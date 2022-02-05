// save some bytes
const gel = (e) => document.getElementById(e);

var batteryLow=false;

function docReady(fn) {
  // see if DOM is already available
  if (
    document.readyState === "complete" ||
    document.readyState === "interactive"
  ) {
    // call on next available tick
    setTimeout(fn, 1);
  } else {
    document.addEventListener("DOMContentLoaded", fn);
  }
}

docReady(async function () {

  gel("ota").addEventListener(
    "click",
    () => {
      gel("main").style.display = "none";
      uploadError(batteryLow?"Battery low. Can't update! Recharge first":null);
    },
    false
  );

  gel("newfile").addEventListener(
    "change",
    () => {
        uploadError(null)
    },
    false
  );

  gel("ota_update").addEventListener(
    "click",
    () => {
        upload()
    },
    false
  );

  gel("go-back").addEventListener(
    "click",
    () => {
      gel("update").style.display = "none";
      gel("main").style.display = "block";
    },
    false
  );

  gel("ok-credits").addEventListener(
    "click",
    () => {
      gel("credits").style.display = "none";
      gel("main").style.display = "block";
    },
    false
  );

  gel("acredits").addEventListener(
    "click",
    () => {
      event.preventDefault();
      gel("main").style.display = "none";
      gel("credits").style.display = "block";
    },
    false
  );

  //first time the page loads, get the battery info
  await getInfo();
});


async function getInfo(url = "info.json") {
  try {
    var res = await fetch(url);
    var info = await res.json();
    console.log(info);
    var h = "";
    var l = (info.mode)?3:5;
    if(l>info.batteries.length) l=info.batteries.length;
    for(var i=0; i<l; i++) {
      let ap_class = i ===  l - 1 ? "" : " brdb";
      h += `<div class="nfo${ap_class}">BAT${i+1}: ${info.batteries[i]/100}V</div>\n`;
    };
    if(info.batteries[0]<360) batteryLow=true;

    gel("voltages").innerHTML = h;
    let charging="Unknown";
    if(info.state==0) charging="Not charging";
    else if(info.state==1) charging="Charging"+(info.mode)?"":" (1)";
    else if(info.state==2) charging="Charging (2)";
    else if(info.state==3) charging="Charging done";
    else charging="Unknown";
    var errCnt = (info.missed>0)?"   (errCnt=${info.missed})":"";
    gel("chargerState").innerHTML = `<div class="nfo">${charging}${errCnt}</div>`;
    gel("version").innerHTML = `<div class="nfo">${info.version}</div>`;
  } catch (e) {
    console.log(e);
    console.info("invalid info returned from /info.json!");
    gel("version").innerHTML = `<div class="nfo">Unknown</div>`;
    gel("voltages").innerHTML = `<div class="nfo">Unknown</div>`;
    gel("chargerState").innerHTML = `<div class="nfo">Unknown</div>`;
  }
}

//Handle file upload for OTA Update
async function upload() {
  try {
    var fileInput = document.getElementById("newfile").files;

    /* Max size of an individual file. Make sure this
     * value is same as that set in file_server.c */
    var MAX_FILE_SIZE = 2*1024*1024;
    var MAX_FILE_SIZE_STR = "2MB";

    if(batteryLow) {
        uploadError("Battery low. Can't update! Recharge first");
    } else if (fileInput.length == 0) {
        uploadError("No file selected!");
    } else if (fileInput[0].size > 2*1024*1024) {
        uploadError("File size must be less than 2MB!");
    } else {
        gel("update").style.display = "none";
        gel("updating").style.display = "block";
        gel("update-error").style.display = "none";
        
        var file = fileInput[0];
        //Send raw binary file (i.e. No FormData)
        var response = await fetch("/ota", { method: 'POST', body: file });
        //Get status code from response
        if(response.ok) {
            //200 means update succeeded. Display restarting message
            gel("update-succes").style.display = "block";
            gel("loading").style.display = "none";
        } else {
            //400 means update failed. Status message should show the reason
            uploadError(response.statusText);
        }
    }
  } catch(ex) {
    console.log(ex)
    uploadError(ex)
  }
}

function uploadError(msg) {
    gel("updating").style.display = "none";
    gel("update").style.display = "block";
    if(msg!=null) {
        //display error message
        gel("update-error").innerHTML = `<h3 class="rd">Update failed: ${msg}</h3>`;  
        gel("update-error").style.display = "block";
    } else {
        //no error message
        gel("update-error").style.display = "none";
    }
}
