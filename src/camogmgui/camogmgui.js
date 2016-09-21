function init() {
	setTimeout('is_hdd_mounted()', 300);
	if (document.format.container[2].checked)
		setTimeout('format_changed(document.format.container[2])', 600);
	setTimeout('update_name_scheme()', 900);
	setTimeout('check_audio_hardware()', 1200);
	setTimeout('update_audio_form(document.getElementById("audioform"))', 1500);
	setTimeout('list_files(getCookie("current_dir"))', 2000);
	setTimeout('calc_split_size()', 2300);
	setTimeout('scan_devices()', 3000);
}
function reload() {
	makeRequest('camogm_interface.php', '?cmd=run_camogm');	
	setTimeout('makeRequest("camogm_interface.php", "?cmd=setmov")', 500); // set MOV as default container format
}
function mount_hdd() {
	makeRequest('camogm_interface.php', '?cmd=mount');
	document.getElementById('directory').value = "/var/hdd/";
	document.getElementById('mount_hdd_button').style.display = "none";
}
function mount_custom_partition(partition) {
	if (document.getElementById("mount_point").value != "") {
		makeRequest('camogm_interface.php', '?cmd=mount&partition=' + partition + '&mountpoint=' + document.getElementById("mount_point").value);
		document.getElementById('directory').value = "/var/hdd/";
		document.getElementById('mount_hdd_button').style.display = "none";

		setTimeout('scan_devices()', 700);
	}
}
function unmount_custom_partition(mountpoint) {
	if (mountpoint != "") {
		makeRequest('camogm_interface.php', '?cmd=umount&mountpoint=' + mountpoint);
		setTimeout('scan_devices()', 700);
	}
}
function process_mount_hdd(xmldoc) {
	var response = xmldoc.getElementsByTagName('mount')[0].firstChild.data;
	if (response == "done")
	{
		setTimeout('is_hdd_mounted()', 500);
		setTimeout('scan_devices()', 800);
	}
}
function is_hdd_mounted() {
	makeRequest('camogm_interface.php', '?cmd=is_hdd_mounted');	
}
function process_is_hdd_mounted(xmldoc) {			
	var response = xmldoc.getElementsByTagName('is_hdd_mounted')[0].firstChild.data;
	if (response == "no HDD mounted")
	{
		document.getElementById('mounted_devices').innerHTML = "no devices found";
		document.getElementById('refresh_file_list').style.display = "none"; // hide "reload" button
		document.getElementById('create_webshare_button').style.display = "none"; // hide "create webshare" button
		document.getElementById('create_folder').style.display = "none"; // hide "create folder" button
		document.getElementById('set_rec_dir').style.display = "none"; // hide "set record directory" button

		// Ignore all odds and just mount it
		setTimeout('mount_hdd()', 1000);
	}
	else // we have a hdd mounted
	{
		document.getElementById('mount_hdd_button').style.display = 'none'; // hide mount button
		document.getElementById('refresh_file_list').style.display = "inline"; // show "reload" button
		document.getElementById('create_webshare_button').style.display = "inline"; // show "create webshare" button
			document.getElementById('create_folder').style.display = "inline"; // show "create folder" button
		document.getElementById('set_rec_dir').style.display = "inline"; // show "set record directory" button
		document.getElementById('mounted_devices').innerHTML = response;
		setTimeout('get_hdd_space()', 300);
		setTimeout('list_files("")', 600);
	}
}
function scan_devices() {
	makeRequest('camogm_interface.php', '?cmd=listdevices');	
}
function process_scan_devices(xmldoc) {			
	if (xmldoc.getElementsByTagName('listdevices').length > 0) {
		var content = "";
		content += "<table cellpadding='10' cellspacing='0' cellmargin='0'>";
		content += "<tr><td></td><td><b>Partition</b></td><td><b>Mountpoint</b></td><td><b>Size</b></td><td><b>Filesystem</b></td><td></td></tr>";
		for (var i=0; i < xmldoc.getElementsByTagName('item').length; i++) {
			if (xmldoc.getElementsByTagName('item')[i].firstChild.firstChild.data != null) {
				if (xmldoc.getElementsByTagName('item')[i].getElementsByTagName('mountpoint')[0].firstChild.data == "none") {
					content += "<tr><td></td><td>" + xmldoc.getElementsByTagName('partition')[i].firstChild.data + "</td><td><input id='mount_point' size='8' type='text'";
					if(xmldoc.getElementsByTagName('partition')[i].firstChild.data == "/dev/hda1")
						content += " value='/var/hdd'";
					content += "></td><td>" + xmldoc.getElementsByTagName('size')[i].firstChild.data + "</td><td>" + '</td><td><a href="#" onClick="mount_custom_partition(\'' + xmldoc.getElementsByTagName('partition')[i].firstChild.data + '\');">mount</a></td></tr>';
				} else { 
					content += "<tr><td><img alt=\"HDD\" src=\"../camerasetup/images/hdd.png\"></td><td>" + xmldoc.getElementsByTagName('partition')[i].firstChild.data;
					content += "</td><td>" + xmldoc.getElementsByTagName('mountpoint')[i].firstChild.data + "</td><td>";
					content += xmldoc.getElementsByTagName('size')[i].firstChild.data + "</td><td>" + xmldoc.getElementsByTagName('filesystem')[i].firstChild.data;
					content += '</td><td><a href="#" onClick="unmount_custom_partition(\' ' + xmldoc.getElementsByTagName('mountpoint')[i].firstChild.data + '\');">unmount</a></td></tr>';	
				}
			}
		}
		content += "</table>";
		document.getElementById('ajax_devices').innerHTML = content;
	}
}
function create_folder() {
	var name_prompt = prompt("Folder Name:");
	if (name_prompt != null)
		makeRequest('camogm_interface.php', '?cmd=mkdir&name=' + getCookie('current_dir') + "/" + name_prompt);
}
function process_mkdir(xmldoc) {
	var sucess = xmldoc.getElementsByTagName('mkdir')[0].firstChild.data;
	if (sucess == "done")
	{
		setTimeout("list_files(getCookie('current_dir'))", 500);
	}
	else
		alert("error creating folder");
}
function get_hdd_space() {
	makeRequest('camogm_interface.php', '?cmd=get_hdd_space');
}
function process_hdd_space(xmldoc) {
	var response = xmldoc.getElementsByTagName('get_hdd_space')[0].firstChild.data;
	document.getElementById('hdd_rem').innerHTML = Math.round(response/1024/1024/1024*100)/100 + " GB";
}
function create_webshare() {
	makeRequest('camogm_interface.php', '?cmd=create_symlink');
	document.getElementById('create_webshare_button').style.display = "none";
	setTimeout('list_files("")', 500);
}
function set_rec_dir() {
	// Show Format Tab
	setCookie('tab', 2, 365);
	TabbedPanels1.showPanel(2);
	
	document.getElementById('directory').value = "/var/hdd" + getCookie('current_dir');
	
	makeRequest('camogm_interface.php', '?cmd=set_prefix&prefix=' + "/var/hdd" + getCookie('current_dir'));
	
	
}
function list_files(dir) {
	if (dir == "")
	{
		makeRequest('camogm_interface.php', '?cmd=list_files');
		setCookie("current_dir", "/", 365);
	}
	else
	{
		makeRequest('camogm_interface.php', '?cmd=list_files&dir=' + dir);
		setCookie("current_dir", dir, 365);
	}	
	document.getElementById('files_current_folder').innerHTML = "/var/hdd" + getCookie("current_dir");
}
function process_list_file(xmldoc) {
	var can_continue = true;
	if (xmldoc.getElementsByTagName('list_files').length > 0) {
		if (xmldoc.getElementsByTagName('list_files')[0].firstChild.data != null) {
			if (xmldoc.getElementsByTagName('list_files')[0].firstChild.data == "no webshare found") {
				can_continue = false;
				document.getElementById('create_webshare_button').style.display = "inline";
				document.getElementById('filelist').innerHTML = "no webshare found<br>";
				
				// ingore all odds and just create the webshare
				setTimeout('create_webshare()', 300);
			}
		}
	}
	if (can_continue)
	{
		var count = xmldoc.getElementsByTagName('file').length;
		var response = "<table cellspacing=\"0px\" cellpadding=\"3px\" width=\"100%\">";
		response  += "<tr><td width=\"50%\"><b>File</b></td><td width=\"30%\"><b>Creation Date</b></td><td><b>Size</b></td></tr>";
		
		for ( var i = 0; i < count; i++ ) {
			document.getElementById('create_webshare_button').style.display = "none";
			var skip = false;
			if (xmldoc.getElementsByTagName('file')[i].childNodes[1].firstChild.data == "lost+found")
				skip = true;
				
			if (!skip) {
				if (i%2 == 0)
					response += '<tr class="file_row_even">';
				else
					response += '<tr class="file_row_odd">';
				response += "<td>";
				type = xmldoc.getElementsByTagName('file')[i].childNodes[0].firstChild.data;
				filename = xmldoc.getElementsByTagName('file')[i].childNodes[1].firstChild.data;
				path = xmldoc.getElementsByTagName('file')[i].childNodes[2].firstChild.data;
				switch (type) {
					case 'updir':
					response += '<a href="#" onClick="list_files(\'' + path + '\');">';
					response += '<img src="images/up_folder.gif"> ';
					response += filename + "</a>";
					break;

					case 'dir':
					response += '<a href="#" onClick="list_files(\'/' + path + '/\');">';
					response += '<img src="images/folder.gif"> ';
					response += filename + "</a>";
					break;
					
					case 'mov':
					response += "<a href=\"http://" + location.host + "/hdd/" + path + "\">";
					response += '<img src="images/quicktime.png"> ';
					response += filename + "</a>";
					break;
					
					default:
					response += "<a href=\"http://" + location.host + "/hdd/" + path + "\">";
					response += filename + "</a>";
					break;
				}
				response += "</td>";
				response += "<td>";
				var date = xmldoc.getElementsByTagName('file')[i].childNodes[4].firstChild.data;
				response += date;
				response += "</td>";
				response += "<td>";
				var size = xmldoc.getElementsByTagName('file')[i].childNodes[3].firstChild.data;
				response += Math.round(size/1024/1024*100)/100 + " MB";
				response += "</td>";
				response += "</tr>";
			}	
		}
		response += "</table><br>";
		document.getElementById('filelist').innerHTML = response;
	}
}
function start_compressor(parent, port) {
	makeRequest('camogm_interface.php', '?cmd=init_compressor', '&sensor_port=' + port);
	parent.style.display = 'none';
}
function update_audio_form(thisform) {
	with (thisform)
	{
		if(audio_check.checked == true) {
			audio_sample_rate.disabled = false;
			audio_channels.disabled = false;
			audio_volume.disabled = false;
			audio_syncmode.disabled = false;
			document.getElementById('ajax_audio_recording').innerHTML = "<span class=\"green\">enabled</span>";
		} else {
			audio_sample_rate.disabled = true;
			audio_channels.disabled = true;		
			audio_volume.disabled = true;
			audio_syncmode.disabled = true;
			if (audio_hardware_detected)
			document.getElementById('ajax_audio_recording').innerHTML = "<span class=\"alert\">disabled</span>";
		}
	}
}
function makeRequest(url, parameters) {
	http_request = false;
	if (window.XMLHttpRequest) { // Mozilla, Safari,...
		http_request = new XMLHttpRequest();
		if (http_request.overrideMimeType) {
			http_request.overrideMimeType('text/xml');
		}
	} else if (window.ActiveXObject) { // IE
		try {
			http_request = new ActiveXObject("Msxml2.XMLHTTP");
		} catch (e) {
			try {
				http_request = new ActiveXObject("Microsoft.XMLHTTP");
			} catch (e) {}
		}
	}
	if (!http_request) {
 		alert('Cannot create XMLHTTP instance');
 		return false;
	}
	http_request.onreadystatechange = process_request;
	http_request.open('GET', url + parameters, true);
	http_request.send(null);
}

function process_request() {
	if (http_request.readyState == 4) {
		if (http_request.status == 200) {
			if(http_request.responseXML != null) {
				var xmldoc = http_request.responseXML;
				if (xmldoc.getElementsByTagName('camogm_state').length > 0) {
					process_recording(xmldoc);
				}
				if (xmldoc.getElementsByTagName('command').length > 0) {
					var command = xmldoc.getElementsByTagName('command')[0].firstChild.data;
					switch (command)
					{
						case "is_hdd_mounted":
							process_is_hdd_mounted(xmldoc);
							break;
						case "list_files":
							process_list_file(xmldoc);
							break;
						case "file_rename":
							process_rename_file(xmldoc);
							break;
						case "get_hdd_space":
							process_hdd_space(xmldoc);
							break;	
						case "mount":
							process_mount_hdd(xmldoc);
							break;	
						case "mkdir":
							process_mkdir(xmldoc);
							break;	
						case "check_audio_hardware":
							process_check_audio_hardware(xmldoc);
							break;	
						case "listdevices":
							process_scan_devices(xmldoc);
							break;
						default:
							break;
					}
				}
			}
		}
	}
}
function process_recording(xmldoc) {
	var file_duration = xmldoc.getElementsByTagName('file_duration')[0].firstChild.data;
	var state = xmldoc.getElementsByTagName('state')[0].firstChild.data;
	var frame_number = xmldoc.getElementsByTagName('frame_number')[0].firstChild.data;
	var file_length = xmldoc.getElementsByTagName('file_length')[0].firstChild.data;
	var file_name = xmldoc.getElementsByTagName('file_name')[0].firstChild.data;
									
	//Update HTML
	document.getElementById('ajax_state').innerHTML = state.substring(1, state.length-1);
	document.getElementById('ajax_file_duration').innerHTML = Math.round(file_duration*100)/100 + " seconds / " + frame_number + " frames";
	document.getElementById('ajax_file_length').innerHTML = Math.round(file_length/1024/1024*100)/100 + " MB";
	document.getElementById('ajax_data_rate').innerHTML = Math.round(file_length/1024/1024/file_duration*800)/100 + " Mbit/s  |  " + Math.round(file_length/1024/1024*100/file_duration)/100 + " MByte/s";
	document.getElementById('ajax_data_rate2').innerHTML = Math.round(file_length/1024/1024/frame_number*800)/100 + " Mbit/frame  |  " + Math.round(file_length/1024/1024/frame_number*100)/100  + " MByte/frame";
	//document.getElementById('ajax_fps').innerHTML = Math.round(frame_number/file_duration*100)/100 + " fps";	
	document.getElementById('ajax_file_name').innerHTML = file_name.substring(1, file_name.length-1);	
	
	//Update Buffer Bar
	var buffer_free = xmldoc.getElementsByTagName('buffer_free')[0].firstChild.data;
	var buffer_used = xmldoc.getElementsByTagName('buffer_used')[0].firstChild.data;
	//var buffer_rp = xmldoc.getElementsByTagName('circbuf_rp')[0].firstChild.data; // buffer read pointer
	
	//var buffer_filled = buffer_used - buffer_rp;
	document.getElementById('buffer_free').style.width = Math.round(buffer_free / 19791872 * 300);
	document.getElementById('buffer_used').style.width = Math.round(buffer_used / 19791872 * 300);

	get_hdd_space();
	
	if (xmldoc.getElementsByTagName('buffer_overruns')[0].firstChild.data > 0)
		alert ("Buffer overrun! current datarate exceeds max. write rate")
}
recording = false;
function update_state() {
	if (recording) {
		makeRequest('camogm_interface.php', '?cmd=status');
		setTimeout('update_state()', 200);
	}
}
function toggle_recording() {
	if (recording) // Stop it
	{
		recording = false;
		makeRequest('camogm_interface.php', '?cmd=stop');
		
		//show it
		document.getElementById('sitecoloumn').style.backgroundColor = "#F4F4F2";
		document.getElementById('record_text').innerHTML = "<img src=\"images/record.gif\" style=\"position:relative; bottom:-5px;\"> RECORD";

		// rename file from user prompt
		if (document.filenames.file_name_scheme[1].checked) {
			var name_prompt = prompt("Rename recorded file to: ");
			if (name_prompt != null)
				rename_file(document.getElementById('ajax_file_name').innerHTML, name_prompt);
			//alert(document.getElementById('ajax_file_name').innerHTML);
		}
		
		// rename file from advanced naming scheme
		if (document.filenames.file_name_scheme[2].checked)
		{
			rename_file(document.getElementById('ajax_file_name').innerHTML, document.getElementById('directory').value + document.filenames.result.value);
		}
		
		setTimeout('list_files(getCookie("current_dir"))', 300);
		setTimeout('get_hdd_space()', 600);
		setTimeout("makeRequest('camogm_interface.php', '?cmd=status')", 900);
	}
	else // Start it
	{
		// Show Status Tab
		setCookie('tab', 0, 365);
		TabbedPanels1.showPanel(0);

		recording = true;
		makeRequest('camogm_interface.php', '?cmd=start');
		
		// show we are recording
		document.getElementById('record_text').innerHTML = "<img src=\"images/stop.gif\" style=\"position:relative; bottom:-5px;\"> STOP";
		document.getElementById('sitecoloumn').style.backgroundColor = "#AF2020";
	}
	update_state();
}
function rename_file(oldname, newname) {
	makeRequest('camogm_interface.php', '?cmd=file_rename&file_old=' + oldname + '&file_new=' + newname);
}
function process_rename_file(xmldoc) {
	var response = xmldoc.getElementsByTagName('file_rename')[0].firstChild.data;
	
	if (response != "done")
		alert ("renaming failed: " + response);
	
	setTimeout('list_files(getCookie("current_dir"))', 300);	
}
function update_fps_reduce(parent) {
	if (document.getElementById('fps_reduce_frameskip').checked) {
		document.getElementById('timelapse_id').disabled = true;
		document.getElementById('frameskip_id').disabled = false;
	}
	if (document.getElementById('fps_reduce_timelapse').checked) {
		document.getElementById('timelapse_id').disabled = false;
		document.getElementById('frameskip_id').disabled = true;
	}
}
function update_live_image() {
	var imgsrv_port = 2323;
	var radios = document.getElementsByName("selected_sensor_port");
	for (var i = 0; i < radios.length; i++) {
		if (radios[i].checked) {
			imgsrv_port = imgsrv_port + i;
			break;
		}
	}
	document.getElementById('live-picture').src = "http://" + location.host + ":" + imgsrv_port + "/bimg?" + Math.random()*99999999999;
}
function size_up_image() {
	document.getElementById('live-picture').width += 40;
	document.getElementById('live-picture').height += 30;

	var old_height = document.getElementById('CollapsiblePanel1Inhalt').style.height;
	document.getElementById('CollapsiblePanel1Inhalt').style.height = parseInt(old_height.substring(0, old_height.length-2)) + 30; + "px";
}
function size_down_image() {
	document.getElementById('live-picture').width -= 40;
	document.getElementById('live-picture').height -= 30;
	
	var old_height = document.getElementById('CollapsiblePanel1Inhalt').style.height;
	document.getElementById('CollapsiblePanel1Inhalt').style.height = parseInt(old_height.substring(0, old_height.length-2)) - 30; + "px";
}
function help(caller) {
	switch (caller) {
		case 'sensor':
			alert("The sensor is not yet in a working state. It needs to be initated first. Tools like camvc should automatically do this for you.");
			break;
		case 'debug':
			alert("The higher the debug-level the more information is written to the debug file (it may slow down camogm and cause it to drop frames even if it could handle it with no/lower debug-level output).")
			break;
		
	}
}
function format_changed(parent) {
	//alert(parent.value); // debug
	if (parent.value == "jpg")
	{
		document.filenames.file_name_scheme[1].disabled = true;
		document.filenames.file_name_scheme[0].checked = true;
		document.getElementById('filename_prompt_text').style.color = "#777777";
	}
	else
	{
		document.filenames.file_name_scheme[1].disabled = false;
		document.getElementById('filename_prompt_text').style.color = "#000";
	}
	update_name_scheme();
}
function filenamesel_changed() {
	if (document.filenames.file_name_scheme[2].checked)
		document.getElementById('advanced_name_panel').style.display = "block";
	else
	{
		document.getElementById('advanced_name_panel').style.display = "none";
	}
	
	var result = "";
	for (var i = 0; i < document.filenames.file_name_scheme.length; i++) {
		if (document.filenames.file_name_scheme[i].checked)
			result = document.filenames.file_name_scheme[i].value;
	}
	
	setCookie('file_name_scheme', result, 365);
}
function update_name_scheme() {
	if (document.filenames.prefix_enable.checked)
		document.filenames.pre_name.disabled = false;
	else
		document.filenames.pre_name.disabled = true;	
	// save all form data so it survives a page reload
	setCookie('filenames_prefix_disabled', document.filenames.pre_name.disabled, 365);
	setCookie('filenames_prefix', document.filenames.pre_name.value, 365);
		
	if (document.filenames.scene_enable.checked)
		document.filenames.scene.disabled = false;
	else
		document.filenames.scene.disabled = true;
	setCookie('filenames_scene_disabled', document.filenames.scene_enable.disabled, 365);
	setCookie('filenames_scene', document.filenames.scene.value, 365);
	
	if (document.filenames.shot_enable.checked)
		document.filenames.shot.disabled = false;
	else
		document.filenames.shot.disabled = true;
	setCookie('filenames_shot_disabled', document.filenames.shot.disabled, 365);
	setCookie('filenames_shot', document.filenames.shot.value, 365);
		
	if (document.filenames.take_enable.checked)
		document.filenames.take.disabled = false;
	else
		document.filenames.take.disabled = true;	
	setCookie('filenames_take_disabled', document.filenames.take.disabled, 365);
	setCookie('filenames_take', document.filenames.take.value, 365);
	
	if (document.filenames.custom_enable.checked)
	{
		document.filenames.custom.disabled = false;
		document.filenames.custom_number.disabled = false;
	}
	else
	{
		document.filenames.custom.disabled = true;	
		document.filenames.custom_number.disabled = true;		
	}
	setCookie('filenames_custom_disabled', document.filenames.custom.disabled, 365);
	setCookie('filenames_custom', document.filenames.custom.value, 365);
	setCookie('filenames_customnumber', document.filenames.custom_number.value, 365);	
	
	// Result
	if (document.filenames.prefix_enable.checked)
		prefix = document.filenames.pre_name.value;
	else
		prefix = "";
		
	if (document.filenames.scene_enable.checked && document.filenames.scene.value != "")
		scene = "_Scene" + document.filenames.scene.value;
	else
		scene = "";

	if (document.filenames.shot_enable.checked && document.filenames.shot.value != "")
		shot = "_Shot" + document.filenames.shot.value;
	else
		shot = "";
		
	if (document.filenames.take_enable.checked && document.filenames.take.value != "")
		take = "_Take" + document.filenames.take.value;
	else
		take = "";
		
	if (document.filenames.custom_enable.checked)
		custom = "_" + document.filenames.custom.value + document.filenames.custom_number.value;
	else
		custom = "";
		
	var extension = "";	
	for (var i = 0; i < document.format.container.length; i++) {
		if (document.format.container[i].checked)
			extension = document.format.container[i].value;
	}
	document.filenames.result.value = prefix + scene + shot + take + custom + "." + extension;
		
}
function raise(target) {
	document.getElementById(target).value++;
	update_name_scheme();
}
function lower(target) {
	if (document.getElementById(target).value != 0)
		document.getElementById(target).value--;
	update_name_scheme();
}
function live_image_auto_update_changed() {
	if (document.getElementById('live_image_auto_update').checked == true) {
		validate_update_freq();
		update_live_image_loop();
	}
}
function validate_update_freq() {
	if (document.getElementById('live_image_auto_update_frequency').value > 60)
		document.getElementById('live_image_auto_update_frequency').value = 60;
	if (document.getElementById('live_image_auto_update_frequency').value < 1)
		document.getElementById('live_image_auto_update_frequency').value = 1;
}
function update_live_image_loop() {
	if (document.getElementById('live_image_auto_update').checked == true) {
		update_live_image();
		setTimeout('update_live_image_loop()', document.getElementById('live_image_auto_update_frequency').value*1000);
	}
}
function setCookie(c_name, value, expiredays) {
	var exdate = new Date();
	exdate.setDate(exdate.getDate() + expiredays);
	document.cookie = c_name + "=" + escape(value) + ((expiredays==null) ? "" : ";expires="+exdate.toGMTString());
}
function getCookie(c_name) {
	if (document.cookie.length>0) {
		c_start = document.cookie.indexOf(c_name + "=");
		if (c_start != -1) { 
			c_start = c_start + c_name.length + 1; 
			c_end = document.cookie.indexOf(";", c_start);
			if (c_end == -1)
				c_end = document.cookie.length;
			return unescape(document.cookie.substring(c_start, c_end));
		} 
	}
	return "";
}

function check_audio_hardware() {
	makeRequest('camogm_interface.php', '?cmd=check_audio_hardware');
}

audio_hardware_detected = false;

function process_check_audio_hardware(xmldoc) {
	var response = xmldoc.getElementsByTagName('check_audio_hardware')[0].firstChild.data;
	
	if (response == "no Audio Hardware detected")
	{
		audio_hardware_detected = false;
		document.getElementById('ajax_detected_audio_hardware').innerHTML = "<span style='color:red;'>" + response + "</span>";
		document.getElementById('audio_sample_rate').disabled = true;
		document.getElementById('audio_check').disabled = true;
		document.getElementById('audio_channels').disabled = true;		
		document.getElementById('audio_check').checked = false;	
		document.getElementById('audio_volume').checked = false;	
		document.getElementById('audio_syncmode').checked = false;			
		document.getElementById('ajax_audio_recording').innerHTML = "disabled";	
		
	}
	else
	{
		audio_hardware_detected = true;
		document.getElementById('ajax_detected_audio_hardware').innerHTML = "<span style='color:green;'>" + response + "</span>";
		document.getElementById('audio_check').disabled = false;	
	}
	setTimeout('update_audio_form(document.getElementById("audioform"))', 300);
}

function test_audio_playback(soundfile) {
	makeRequest('camogm_interface.php', '?cmd=test_audio_playback&soundfile=' + soundfile);
}

function calc_split_size() {
	if (document.getElementById('split_size_input').value > 1000000000)
		var result = Math.round(document.getElementById('split_size_input').value/1024/1024/1024*100)/100 + " GB";	
	else
		var result = Math.round(document.getElementById('split_size_input').value/1024/1024*100)/100 + " MB";	
	//alert(result);
	document.getElementById('split_size_calculation').innerHTML = " = " + result;
}
function toggle_buffer() {
	if (document.getElementById('buffer_bar').style.display == "none") {
		document.getElementById('buffer_bar').style.display = "inline";
		document.getElementById('buffer_toggle_link').style.display = "none";
	}
	else {
		document.getElementById('buffer_bar').style.display = "none";
		document.getElementById('buffer_toggle_link').style.display = "block";
	}
}
