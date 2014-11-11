var busy = false;

function runCommand(cmd, callback) {
	callback = callback || false
    if(!busy) {
        busy = true;
        var url = $("#address").val() + (callback ? "/command" : "/command_silent");
        cmd += "\n";
        $.post( url, cmd, callback)
            .fail( function() { log("No contact with printer!"); })
            .always( function() { busy = false; });
    } else {
        log("Busy!");    
    }
}

function log(msg) {
    var e = $("#commandlog");
    e.append( msg + "\n" );
    e.scrollTop(e.prop("scrollHeight"));
}

function logCommand(cmd, callback) {
    log(cmd);
    runCommand( cmd, function( data ) {
        log(data);
        if(callback) callback(data);
    });
}

function send(event) {
    cmd = $('#commandText').val();
    logCommand(cmd);
    $('#commands').append('<option value="'+cmd+'"></option>');
}

function jogXYClick (cmd) {
  runCommand("G91 G0 " + cmd + " F" + $("#xy_velocity").val() + " G90")
}

function jogZClick (cmd) {
  runCommand("G91 G0 " + cmd + " F" + $("#z_velocity").val() + " G90")
}

function extrude(event,a,b) {
  var length = $("#extrude_length").val();
  var velocity = $("#extrude_velocity").val();
  var direction = (event.currentTarget.id=='extrude')?1:-1;
  runCommand("G91 G0 E" + (length * direction) + " F" + velocity + " G90");
}

function motorsOff(event) {
  runCommand("M18");
}

function heatSet(event) {
  var type = (event.currentTarget.id=='heat_set')?104:140;
  var temperature = (type==104) ? $("#heat_value").val() : $("#bed_value").val();
  runCommand("M" + type + " S" + temperature);
}

function heatOff(event) {
  var type = (event.currentTarget.id=='heat_off')?104:140;
  runCommand("M" + type + " S0");
}
function getTemperature () {
  logCommand("M105", false);
}

$(function(){
    var data1 = [],
        data2 = [],
        totalPoints = 100,
        graphTimer;

    var plot = $.plot("#tempGraph", [], {
        series: {
            shadowSize: 0   // Drawing is faster without shadows
        },
        yaxis: {
            min: 0
        },
        xaxis: {
            min: 0,
            max: totalPoints,
            show: false
        },
        colors: [ "#d00000", "#e02020", "#0000d0", "#2020e0" ],
        legend: { position: "nw" }
    });
    plot.draw();

    function pushMeas(val, data) {
        if (data.length > totalPoints) {
            data.shift();
        }
        
        data.push(val);

        var res = [];
        for (var i = 0; i < data.length; ++i) {
            res.push([i, data[i]])
        }
        return res;
    }

    function addTemp(str) {
        var regex = /([A-Z]):([0-9.]+) *\/([0-9.]+).*([A-Z]):([0-9.]+) *\/([0-9.]+)/;
        var match = regex.exec(str);
        
        var res1 = pushMeas(Number(match[2]), data1);
        var res2 = pushMeas(Number(match[5]), data2);
        
        var set1 = Number(match[3]);
        var set2 = Number(match[6]);
        
        plot.setData([ 
            { label: match[1], lines: { lineWidth: 3 }, data: res1 }, 
            { lines: { lineWidth: 1 }, data: [[0, set1],[totalPoints, set1]] },
            { label: match[4], lines: { lineWidth: 3 }, data: res2 },
            { lines: { lineWidth: 1 }, data: [[0, set2],[totalPoints, set2]] }]);
        plot.setupGrid();
        plot.draw();
    }

    var xx = 0;
    function fake() {
        xx += 0.05;
        return "T:"+(100+Math.sin(xx)*100)+"/200 @0 "+
               "B:"+(50+Math.cos(xx)*50)+"/55 @0";
    }

    function updateGraph() {
        if($("#sim").is(":checked")) {
            addTemp(fake());
        } else {
            runCommand("M105", addTemp);
        }
    }

    var graphInterval;
    $("#enablegraph").click(function(event) {
        if(this.checked) {
            var interval = $("#updateInterval").val();
            graphInterval = setInterval(updateGraph, interval);
        } else {
            clearInterval(graphInterval);
        }    
    });
});

function fanSet(event) {
  runCommand("M106 S" + $("#fan_value").val());
}

function fanOff() {
  $("#fan_value").val(0);
  runCommand("M107");
}

$(function() {
  document.getElementById('files').addEventListener('change', handleFileSelect, false);
});

function handleFileSelect(evt) {
    var files = evt.target.files; // handleFileSelectist object

    // files is a FileList of File objects. List some properties.
    var output = [];
    for (var i = 0, f; f = files[i]; i++) {
        output.push('<li><strong>', escape(f.name), '</strong> (', f.type || 'n/a', ') - ',
            f.size, ' bytes, last modified: ',
            f.lastModifiedDate ? f.lastModifiedDate.toLocaleDateString() : 'n/a',
            '</li>');
    }
    document.getElementById('list').innerHTML = '<ul>' + output.join('') + '</ul>';
}

function upload() {
    $( "#progress" ).empty();
    $( "#uploadresult" ).empty();

    // take the file from the input
    var file = document.getElementById('files').files[0];
    var reader = new FileReader();
    reader.readAsBinaryString(file); // alternatively you can use readAsDataURL
    reader.onloadend  = function(evt)
    {
        // create XHR instance
        xhr = new XMLHttpRequest();

        // send the file through POST
        xhr.open("POST", $("#address").val() + '/upload', true);
        xhr.setRequestHeader('X-Filename', file.name);

        // make sure we have the sendAsBinary method on all browsers
        XMLHttpRequest.prototype.mySendAsBinary = function(text){
            var data = new ArrayBuffer(text.length);
            var ui8a = new Uint8Array(data, 0);
            for (var i = 0; i < text.length; i++) ui8a[i] = (text.charCodeAt(i) & 0xff);

            if(typeof window.Blob == "function")
            {
                 var blob = new Blob([data]);
            }else{
                 var bb = new (window.MozBlobBuilder || window.WebKitBlobBuilder || window.BlobBuilder)();
                 bb.append(data);
                 var blob = bb.getBlob();
            }

            this.send(blob);
        }

        // let's track upload progress
        var eventSource = xhr.upload || xhr;
        eventSource.addEventListener("progress", function(e) {
            // get percentage of how much of the current file has been sent
            var position = e.position || e.loaded;
            var total = e.totalSize || e.total;
            var percentage = Math.round((position/total)*100);

            // here you should write your own code how you wish to proces this
            $( "#progress" ).empty().append('uploaded ' + percentage + '%');
        });

        // state change observer - we need to know when and if the file was successfully uploaded
        xhr.onreadystatechange = function()
        {
            if(xhr.readyState == 4)
            {
                if(xhr.status == 200)
                {
                    // process success
                    $( "#uploadresult" ).empty().append( 'Uploaded Ok');
                }else{
                    // process error
                    $( "#uploadresult" ).empty().append( 'Uploaded Failed');
                }
            }
        };

        // start sending
        xhr.mySendAsBinary(evt.target.result);
    };
}

function playFile(filename) {
  runCommand("play /sd/"+filename);
}

function refreshFiles() {
  document.getElementById('fileList').innerHTML = '';
  runCommand("M20", function(data){
    $.each(data.split('\n'), function(index) {
      var item = this.trim();
        if (item.match(/\.g(code)?$/)) {
          var table = document.getElementById('fileList');
          var row = table.insertRow(-1);
          var cell = row.insertCell(0);
          var text = document.createTextNode(item);
          cell.appendChild(text);
          cell = row.insertCell(1);
          cell.innerHTML = "[<a href='javascript:void(0);' onclick='playFile(\""+item+"\");'>Play</a>]";
        }
        //$( "#result" ).append( this + '<br/>' );
      });
  });
}
