/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

// TODO : THIS FILE IS LAME, MUST BE MADE MUCH BETTER

#include "libs/Module.h"
#include "libs/Kernel.h"
#include <math.h>
#include "TemperatureControl.h"
#include "TemperatureControlPool.h"
#include "libs/Pin.h"
#include "modules/robot/Conveyor.h"
#include "PublicDataRequest.h"

#include "PublicData.h"
#include "ToolManagerPublicAccess.h"
#include "StreamOutputPool.h"
#include "Config.h"
#include "checksumm.h"
#include "Gcode.h"
#include "SlowTicker.h"
#include "Pauser.h"
#include "ConfigValue.h"
#include "PID_Autotuner.h"

// Temp sensor implementations:
#include "Thermistor.h"
#include "max31855.h"

#include "MRI_Hooks.h"

#define UNDEFINED -1

#define sensor_checksum                    CHECKSUM("sensor")

#define readings_per_second_checksum       CHECKSUM("readings_per_second")
#define max_pwm_checksum                   CHECKSUM("max_pwm")
#define pwm_frequency_checksum             CHECKSUM("pwm_frequency")
#define bang_bang_checksum                 CHECKSUM("bang_bang")
#define hysteresis_checksum                CHECKSUM("hysteresis")
#define heater_pin_checksum                CHECKSUM("heater_pin")

#define get_m_code_checksum                CHECKSUM("get_m_code")
#define set_m_code_checksum                CHECKSUM("set_m_code")
#define set_and_wait_m_code_checksum       CHECKSUM("set_and_wait_m_code")

#define designator_checksum                CHECKSUM("designator")

#define p_factor_checksum                  CHECKSUM("p_factor")
#define i_factor_checksum                  CHECKSUM("i_factor")
#define d_factor_checksum                  CHECKSUM("d_factor")

#define i_max_checksum                     CHECKSUM("i_max")

#define preset1_checksum                   CHECKSUM("preset1")
#define preset2_checksum                   CHECKSUM("preset2")

TemperatureControl::TemperatureControl(uint16_t name, int index)
{
    name_checksum_ = name;
    pool_index_ = index;
    waiting_ = false;
    min_temp_violated_ = false;
    sensor_ = nullptr;
    readonly_ = false;
}

TemperatureControl::~TemperatureControl()
{
    delete sensor_;
}

void TemperatureControl::on_module_loaded()
{

    // We start not desiring any temp
    target_temperature_ = UNDEFINED;

    // Settings
    load_config();

    // Register for events
    register_for_event(ON_GCODE_RECEIVED);
    register_for_event(ON_GET_PUBLIC_DATA);

    if(!readonly_) {
        register_for_event(ON_GCODE_EXECUTE);
        register_for_event(ON_SECOND_TICK);
        register_for_event(ON_MAIN_LOOP);
        register_for_event(ON_SET_PUBLIC_DATA);
        register_for_event(ON_HALT);
    }
}

void TemperatureControl::on_halt(void *arg)
{
    // turn off heater
    o_ = 0;
    heater_pin_.set(0);
    target_temperature_ = UNDEFINED;
}

void TemperatureControl::on_main_loop(void *argument)
{
#if 0
	// SPI communication should be done on the main loop, so this
	// assumes that this function gets called fairly often.
	uint32_t now = us_ticker_read();
	if (now - last_time_ > PIDdt_ * 1e6 / 4) {
		last_reading_ = sensor_->get_temperature();
		last_time_ = now;
	}
#endif
    if (min_temp_violated_) {
        THEKERNEL->streams->printf("Error: MINTEMP triggered. Check your temperature sensors!\n");
        min_temp_violated_ = false;
    }
}

// Get configuration from the config file
void TemperatureControl::load_config()
{

    // General config
    set_m_code_          = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, set_m_code_checksum)->by_default(104)->as_number();
    set_and_wait_m_code_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, set_and_wait_m_code_checksum)->by_default(109)->as_number();
    get_m_code_          = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, get_m_code_checksum)->by_default(105)->as_number();
    readings_per_second_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, readings_per_second_checksum)->by_default(20)->as_number();

    designator_          = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, designator_checksum)->by_default(string("T"))->as_string();

    // Heater pin
    heater_pin_.from_string( THEKERNEL->config->value(temperature_control_checksum, name_checksum_, heater_pin_checksum)->by_default("nc")->as_string());
    if(heater_pin_.connected()){
        readonly_= false;
        heater_pin_.as_output();

    } else {
        readonly_= true;
    }

    // For backward compatibility, default to a thermistor sensor.
    std::string sensor_type = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, sensor_checksum)->by_default("thermistor")->as_string();

    // Instantiate correct sensor (TBD: TempSensor factory?)
    delete sensor_;
    sensor_ = nullptr; // In case we fail to create a new sensor.
    if(sensor_type.compare("thermistor") == 0) {
        sensor_ = new Thermistor();
    } else if(sensor_type.compare("max31855") == 0) {
        sensor_ = new Max31855();
    } else {
        sensor_ = new TempSensor(); // A dummy implementation
    }
    sensor_->update_config(temperature_control_checksum, name_checksum_);

    preset1_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, preset1_checksum)->by_default(0)->as_number();
    preset2_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, preset2_checksum)->by_default(0)->as_number();


    // sigma-delta output modulation
    o_ = 0;

    if(!readonly_) {
        // used to enable bang bang control of heater
        use_bangbang_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, bang_bang_checksum)->by_default(false)->as_bool();
        hysteresis_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, hysteresis_checksum)->by_default(2)->as_number();
        heater_pin_.max_pwm( THEKERNEL->config->value(temperature_control_checksum, name_checksum_, max_pwm_checksum)->by_default(255)->as_number() );
        heater_pin_.set(0);
        set_low_on_debug(heater_pin_.port_number, heater_pin_.pin);
        // activate SD-DAC timer
        THEKERNEL->slow_ticker->attach( THEKERNEL->config->value(temperature_control_checksum, name_checksum_, pwm_frequency_checksum)->by_default(2000)->as_number(), &heater_pin_, &Pwm::on_tick);
    }


    // reading tick
    THEKERNEL->slow_ticker->attach( readings_per_second_, this, &TemperatureControl::thermistor_read_tick );
    PIDdt_ = 1.0 / readings_per_second_;

    // PID
    setPIDp( THEKERNEL->config->value(temperature_control_checksum, name_checksum_, p_factor_checksum)->by_default(10 )->as_number() );
    setPIDi( THEKERNEL->config->value(temperature_control_checksum, name_checksum_, i_factor_checksum)->by_default(0.3f)->as_number() );
    setPIDd( THEKERNEL->config->value(temperature_control_checksum, name_checksum_, d_factor_checksum)->by_default(200)->as_number() );

    if(!readonly_) {
        // set to the same as max_pwm by default
        i_max_ = THEKERNEL->config->value(temperature_control_checksum, name_checksum_, i_max_checksum   )->by_default(heater_pin_.max_pwm())->as_number();
    }

    iTerm_ = 0.0;
    lastInput_ = -1.0;
    last_reading_ = 0.0;
	last_time_ = us_ticker_read();
}

void TemperatureControl::on_gcode_received(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);
    if (gcode->has_m) {

        if( gcode->m == get_m_code_ ) {
            char buf[32]; // should be big enough for any status
            int n = snprintf(buf, sizeof(buf), "%s:%3.1f /%3.1f @%d ", designator_.c_str(), get_temperature(), ((target_temperature_ == UNDEFINED) ? 0.0 : target_temperature_), o_);
            gcode->txt_after_ok.append(buf, n);
            
            // Output extra diagnostics using letter D.
            if(gcode->has_letter('D'))
            {
                gcode->txt_after_ok.append("("+sensor_->get_diagnostics()+") ");
            }
            gcode->mark_as_taken();
            return;
        }

        // readonly sensors don't handle the rest
        if(readonly_) return;

        if (gcode->m == 301) {
            gcode->mark_as_taken();
            if (gcode->has_letter('S') && (gcode->get_value('S') == pool_index_)) {
                if (gcode->has_letter('P'))
                    setPIDp( gcode->get_value('P') );
                if (gcode->has_letter('I'))
                    setPIDi( gcode->get_value('I') );
                if (gcode->has_letter('D'))
                    setPIDd( gcode->get_value('D') );
                if (gcode->has_letter('X'))
                    i_max_    = gcode->get_value('X');
            }
            //gcode->stream->printf("%s(S%d): Pf:%g If:%g Df:%g X(I_max):%g Pv:%g Iv:%g Dv:%g O:%d\n", designator_.c_str(), pool_index_, p_factor_, i_factor_/PIDdt_, d_factor_*PIDdt_, i_max_, p_, i_, d_, o);
            gcode->stream->printf("%s(S%d): Pf:%g If:%g Df:%g X(I_max):%g O:%d\n", designator_.c_str(), pool_index_, p_factor_, i_factor_ / PIDdt_, d_factor_ * PIDdt_, i_max_, o_);

        } else if (gcode->m == 500 || gcode->m == 503) { // M500 saves some volatile settings to config override file, M503 just prints the settings
            gcode->stream->printf(";PID settings:\nM301 S%d P%1.4f I%1.4f D%1.4f\n", pool_index_, p_factor_, i_factor_ / PIDdt_, d_factor_ * PIDdt_);
            gcode->mark_as_taken();

        } else if( ( gcode->m == set_m_code_ || gcode->m == set_and_wait_m_code_ ) && gcode->has_letter('S')) {
            // this only gets handled if it is not controlle dby the tool manager or is active in the toolmanager
            active_ = true;

            // this is safe as old configs as well as single extruder configs the toolmanager will not be running so will return false
            // this will also ignore anything that the tool manager is not controlling and return false, otherwise it returns the active tool
            void *returned_data;
            bool ok = PublicData::get_value( tool_manager_checksum, is_active_tool_checksum, name_checksum_, &returned_data );
            if (ok) {
                uint16_t active_tool_name =  *static_cast<uint16_t *>(returned_data);
                active_ = (active_tool_name == name_checksum_);
            }

            if(active_) {
                // Attach gcodes to the last block for on_gcode_execute
                THEKERNEL->conveyor->append_gcode(gcode);

                // push an empty block if we have to wait, so the Planner can get things right, and we can prevent subsequent non-move gcodes from executing
                if (gcode->m == set_and_wait_m_code_) {
                    // ensure that no subsequent gcodes get executed with our M109 or similar
                    THEKERNEL->conveyor->queue_head_block();
                }
            }
        }
    }
}

void TemperatureControl::on_gcode_execute(void *argument)
{
    Gcode *gcode = static_cast<Gcode *>(argument);
    if( gcode->has_m) {
        if (((gcode->m == set_m_code_) || (gcode->m == set_and_wait_m_code_))
            && gcode->has_letter('S') && active_) {
            float v = gcode->get_value('S');

            if (v == 0.0) {
                target_temperature_ = UNDEFINED;
                heater_pin_.set((o_ = 0));
            } else {
                set_desired_temperature(v);

                if( gcode->m == set_and_wait_m_code_ && !waiting_) {
                    THEKERNEL->pauser->take();
                    waiting_ = true;
                }
            }
        }
    }
}

void TemperatureControl::on_get_public_data(void *argument)
{
    PublicDataRequest *pdr = static_cast<PublicDataRequest *>(argument);

    if(!pdr->starts_with(temperature_control_checksum)) return;

    if(pdr->second_element_is(pool_index_checksum)) {
        // asking for our instance pointer if we have this pool_index
        if(pdr->third_element_is(pool_index_)) {
            static void *return_data;
            return_data = this;
            pdr->set_data_ptr(&return_data);
            pdr->set_taken();
        }
        return;

    }else if(!pdr->second_element_is(name_checksum_)) return;

    // ok this is targeted at us, so send back the requested data
    if(pdr->third_element_is(current_temperature_checksum)) {
        public_data_return_.current_temperature = get_temperature();
        public_data_return_.target_temperature = (target_temperature_ == UNDEFINED) ? 0 : target_temperature_;
        public_data_return_.pwm = o_;
        public_data_return_.designator= designator_;
        pdr->set_data_ptr(&public_data_return_);
        pdr->set_taken();
    }

}

void TemperatureControl::on_set_public_data(void *argument)
{
    PublicDataRequest *pdr = static_cast<PublicDataRequest *>(argument);

    if(!pdr->starts_with(temperature_control_checksum)) return;

    if(!pdr->second_element_is(name_checksum_)) return;

    // ok this is targeted at us, so set the temp
    float t = *static_cast<float *>(pdr->get_data_ptr());
    set_desired_temperature(t);
    pdr->set_taken();
}

void TemperatureControl::set_desired_temperature(float desired_temperature)
{
    if (desired_temperature == 1.0)
        desired_temperature = preset1_;
    else if (desired_temperature == 2.0)
        desired_temperature = preset2_;

    target_temperature_ = desired_temperature;
    if (desired_temperature == 0.0)
        heater_pin_.set((o_ = 0));
}

float TemperatureControl::get_temperature()
{
    return last_reading_;
}

uint32_t TemperatureControl::thermistor_read_tick(uint32_t dummy)
{
    last_reading_ = sensor_->get_temperature();
	float temperature = last_reading_;
#if 0
	// Check that main loop is not stuck. In that case, turn off.
	if(us_ticker_read() - last_time_ > 100000) {
        target_temperature_ = UNDEFINED;
        heater_pin_.set((o_ = 0));
	}
#endif
    if(readonly_) {
        return 0;
    }
    if (target_temperature_ > 0) {
        if (isinf(temperature)) {
            min_temp_violated_ = true;
            target_temperature_ = UNDEFINED;
            heater_pin_.set((o_ = 0));
        } else {
            pid_process(temperature);
            if ((temperature > target_temperature_) && waiting_) {
                THEKERNEL->pauser->release();
                waiting_ = false;
            }
        }
    } else {
        heater_pin_.set((o_ = 0));
    }
    return 0;
}

/**
 * Based on https://github.com/br3ttb/Arduino-PID-Library
 */
void TemperatureControl::pid_process(float temperature)
{
    if(use_bangbang_) {
        // bang bang is very simple, if temp is < target - hysteresis turn on full else if  temp is > target + hysteresis turn heater off
        // good for relays
        if(temperature > (target_temperature_ + hysteresis_) && o_ > 0) {
            heater_pin_.set(false);
            o_ = 0; // for display purposes only

        } else if(temperature < (target_temperature_ - hysteresis_) && o_ <= 0) {
            if(heater_pin_.max_pwm() >= 255) {
                // turn on full
                heater_pin_.set(true);
                o_ = 255; // for display purposes only
            } else {
                // only to whatever max pwm is configured
                heater_pin_.pwm(heater_pin_.max_pwm());
                o_ = heater_pin_.max_pwm(); // for display purposes only
            }
        }
        return;
    }

    // regular PID control
    float error = target_temperature_ - temperature;
    iTerm_ += (error * i_factor_);
    if (iTerm_ > i_max_) iTerm_ = i_max_;
    else if (iTerm_ < 0.0) iTerm_ = 0.0;

    if(lastInput_ < 0.0) lastInput_ = temperature; // set first time
    float d = (temperature - lastInput_);

    // calculate the PID output
    // TODO does this need to be scaled by max_pwm/256? I think not as p_factor already does that
    o_ = (p_factor_ * error) + iTerm_ - (d_factor_ * d);

    if (o_ >= heater_pin_.max_pwm())
        o_ = heater_pin_.max_pwm();
    else if (o_ < 0)
        o_ = 0;

    heater_pin_.pwm(o_);
    lastInput_ = temperature;
}

void TemperatureControl::on_second_tick(void *argument)
{
    if (waiting_)
        THEKERNEL->streams->printf("%s:%3.1f /%3.1f @%d\n", designator_.c_str(), get_temperature(), ((target_temperature_ == UNDEFINED) ? 0.0 : target_temperature_), o_);
}

void TemperatureControl::setPIDp(float p)
{
    p_factor_ = p;
}

void TemperatureControl::setPIDi(float i)
{
    i_factor_ = i * PIDdt_;
}

void TemperatureControl::setPIDd(float d)
{
    d_factor_ = d / PIDdt_;
}
