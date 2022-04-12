/** 
 * @file format_disk.js
 * @brief Disk formatting front end for Elphel393 series camera
 * @copyright Copyright (C) 2017 Elphel Inc.
 * @author Mikhail Karpenko <mikhail@elphel.com>
 *
 * @licstart  The following is the entire license notice for the 
 * JavaScript code in this page.
 *
 *   The JavaScript code in this page is free software: you can
 *   redistribute it and/or modify it under the terms of the GNU
 *   General Public License (GNU GPL) as published by the Free Software
 *   Foundation, either version 3 of the License, or (at your option)
 *   any later version.  The code is distributed WITHOUT ANY WARRANTY;
 *   without even the implied warranty of MERCHANTABILITY or FITNESS
 *   FOR A PARTICULAR PURPOSE.  See the GNU GPL for more details.
 *
 *   As additional permission under GNU GPL version 3 section 7, you
 *   may distribute non-source (e.g., minimized or compacted) forms of
 *   that code without the copy of the GNU GPL normally required by
 *   section 4, provided you include this license notice and a URL
 *   through which recipients can access the Corresponding Source.
 *
 *  @licend  The above is the entire license notice
 *  for the JavaScript code in this page.
 */

function init_actions() {
	$("#btn_format").click(function() {
		var btn_class_disabled = "btn btn-danger disabled";
		if ($("#btn_format").attr("class") != btn_class_disabled) {
			// introduce this var to fasicilitate further upgrades
			var index = 0;
			var disk_path = $("#path_cell_" + index).text()
			
			force = '';
			if ($("#chk_force").is(':checked'))
				force = '&force';
			else
				force = '';
			noraw = '';
			if ($("#chk_noraw").is(':checked'))
				noraw = '&noraw';
			else
				noraw = '';
			reformat = '';
			if ($("#chk_reformat").is(':checked'))
				reformat = '&reformat';
			else
				reformat = '';
				
				
			$("#status_cell_" + index).text("Formatting");
			$.ajax({
				url:"format_disk.php?cmd=format&disk_path=" + disk_path + force + noraw + reformat,
				success: function(result) {
					if (result == "OK") {
						$("#status_cell_" + index).text("Done");
						$("#btn_format").attr("class", btn_class_disabled);
						$("#chk_force").attr("disabled", true);
					} else {
						if (result == "")
							result = "Unrecognized error";
						console.log(result);
						$("#status_cell_" + index).text(result);
						$("#disk_row_" + index).attr("class", "danger");
					}
				}
			})
		}
	});
}
