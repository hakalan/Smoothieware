/*
      This file is part of Smoothie (http://smoothieware.org/). The motion control part is heavily based on Grbl (https://github.com/simen/grbl).
      Smoothie is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
      Smoothie is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
      You should have received a copy of the GNU General Public License along with Smoothie. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libs/Kernel.h"
#include <math.h>
#include "libs/Pin.h"
#include "Config.h"
#include "checksumm.h"
#include "ConfigValue.h"

#include "max31855.h"

#include "MRI_Hooks.h"

#define chip_select_checksum CHECKSUM("chip_select_pin")
#define spi_channel_checksum CHECKSUM("spi_channel")

Max31855::Max31855() :
    spi(nullptr),
    error_count(0),
    diagnostics(0)
{
}

Max31855::~Max31855()
{
    delete spi;
}

// Get configuration from the config file
void Max31855::update_config(uint16_t module_checksum, uint16_t name_checksum)
{
    // Chip select
    this->spi_cs_pin.from_string(THEKERNEL->config->value(module_checksum, name_checksum, chip_select_checksum)->by_default("0.16")->as_string());
    this->spi_cs_pin.set(true);
    this->spi_cs_pin.as_output();
    
    // select which SPI channel to use
    int spi_channel = THEKERNEL->config->value(module_checksum, name_checksum, spi_channel_checksum)->by_default(0)->as_number();
    PinName miso;
    PinName mosi;
    PinName sclk;
    if(spi_channel == 0) {
        // Channel 0
        mosi=P0_18; miso=P0_17; sclk=P0_15;
    } else {
        // Channel 1
        mosi=P0_9; miso=P0_8; sclk=P0_7;
    } 

    delete spi;
    spi = new mbed::SPI(mosi, miso, sclk);

    // Spi settings: 1MHz (default), 16 bits, mode 0 (default)
    spi->format(16);
}

float Max31855::get_temperature()
{
    static int contiguous_errcnt = 0;
    
    // Return an average of the last readings
    if (readings.size() >= readings.capacity()) {
        readings.delete_tail();
    }

    float temp = read_temp();

    // Discard occasional errors...
    if(isinf(temp))
    {
        ++contiguous_errcnt;
        if(contiguous_errcnt>3)
        {
            return infinityf();
        }
    }
    else
    {
        contiguous_errcnt = 0;
        readings.push_back(temp);
    }

    if(readings.size()==0) return infinityf();

    float sum = 0;
    for (int i=0; i<readings.size(); i++)
        sum += *readings.get_ref(i);

    return sum / readings.size();
}

float Max31855::read_temp()
{
    float temperature;

    this->spi_cs_pin.set(false);
    wait_us(1); // Must wait for first bit valid

    // Read 16 bits (writing something as well is required by the api)
    uint16_t data = spi->write(0);
    //  Read next 16 bits (diagnostics)
    uint16_t data2 = spi->write(0);

    // Store temperature and error flags (sticky) 
    this->diagnostics = data2 | (this->diagnostics&0x7);

    this->spi_cs_pin.set(true);
        
    //Process temp
    if (data & 0x0003)
    {
        // Error flag.
        temperature = infinityf();
        this->error_count++;
    }
    else
    {
        data = data >> 2;

        if (data & 0x2000)
        {
            temperature = (~data + 1) / -4.f;
        }
        else
        {
            temperature = data / 4.f;       
        }
    }
    
    return temperature; 
}

string Max31855::get_diagnostics()
{
    // Determine cold junction temperature.
    float cold_junction_temp;
    uint16_t data2 = diagnostics >> 4;
    if(data2 & 0x800)
    {
        cold_junction_temp = (~data2 + 1) / -16.f; 
    }
    else
    {
        cold_junction_temp = data2 / 16.f;
    }

    char buf[64];
    sprintf(buf, "%d errors: 0x%x, %.1f deg", this->error_count, this->diagnostics&0x7, cold_junction_temp);
    return string(buf);
}