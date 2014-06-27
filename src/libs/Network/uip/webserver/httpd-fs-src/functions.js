
function runCommand(cmd, silent) {
  // Add to command log
  silent = silent || false;
  var option = document.createElement("option");
  option.text = cmd;

  cmd += "\n";
  var url = $("#address").val();
  url += silent ? "/command_silent" : "/command";
  // Send the data using post
  if(silent) {
    $.post( url, cmd );
  } else {
  // Put the results in a div
    document.getElementById("commandlog").add(option, 0);
    $.post( url, cmd, function( data ) {
      $( "#result" ).empty();
      $.each(data.split('\n'), function(index) {
        $( "#result" ).append( this + '<br/>' );
      });
    });
  }
}

function runCommandSilent(cmd) {
  runCommand(cmd);
}

xx = 0;
function fake() {
	xx += 0.05;
	return "T:"+(100+Math.sin(xx)*100)+" B:50";
}

function runCommandCallback(cmd,callback) {
    var url = "/command";
    cmd += "\n";
    $.post( url, cmd, callback);
	callback(fake());
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
  runCommand("M105", false);
}

var plot,
	data1 = [],
	data2 = [],
	totalPoints = 100,
	updateInterval = 100;
var graphTimer;

$( function(){
	$("#updateInterval").val(updateInterval).change(function () {
		var v = $(this).val();
		if (v && !isNaN(+v)) {
			updateInterval = +v;
		}
	});
});

function startGraph() {
	updateGraph();
}

function pushData(val, data) {
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
	var regex = /[A-Z]:([0-9.]+).*[A-Z]:([0-9.]+)/;
	var match = regex.exec(str);
	
/*	if (data1.length > totalPoints) {
		data1 = data1.slice(1);
		data2 = data2.slice(1);
	}
	
	data1.push(Number(match[1]));
	data2.push(Number(match[2]));

	var res1 = [];
	var res2 = [];
	for (var i = 0; i < data1.length; ++i) {
		res1.push([i, data1[i]])
		res2.push([i, data2[i]])
	}
*/
	var res1 = pushData(Number(match[1]), data1);
	var res2 = pushData(Number(match[2]), data2);
	
	plot = $.plot("#tempGraph", [res1, res2], {
		series: {
			shadowSize: 0	// Drawing is faster without shadows
		},
		yaxis: {
			min: 0
		},
		xaxis: {
			min: 0,
			max: totalPoints,
			show: false
		}
	});

	plot.draw();
}

function updateGraph() {
	runCommandCallback("M105", addTemp);

	graphTimer = setTimeout(updateGraph, updateInterval);
}

function stopGraph() {
  clearInterval(graphTimer);
}

function fanSet(event) {
  runCommand("M106 S" + $("#fan_value").val());
}

function fanOff() {
  $("#fan_value").val(0);
  runCommand("M107");
}

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
        xhr.open("POST", 'upload', true);
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
  runCommandSilent("play /sd/"+filename);
}

function refreshFiles() {
  document.getElementById('fileList').innerHTML = '';
  runCommandCallback("M20", function(data){
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
