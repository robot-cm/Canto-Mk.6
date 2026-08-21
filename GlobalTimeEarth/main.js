import * as THREE from "three";


import {
OrbitControls
}
from
"three/addons/controls/OrbitControls.js";



import {

createEarth,

latLonToVector3,

rotateEarth

}

from "./earth.js";



import {

createLocationEffect,

updateEffects

}

from "./effects.js";





let scene;

let camera;

let renderer;

let controls;

let earth;



let autoRotate=true;



let clock =
new THREE.Clock();





const cities={


Tokyo:{

lon:139.6917,

lat:35.6895,

zone:"Asia/Tokyo"

},



Beijing:{

lon:116.4074,

lat:39.9042,

zone:"Asia/Shanghai"

},



Paris:{

lon:2.3522,

lat:48.8566,

zone:"Europe/Paris"

},



"New York":{

lon:-74.006,

lat:40.7128,

zone:"America/New_York"

}



};







init();







function init(){



scene =
new THREE.Scene();





camera =
new THREE.PerspectiveCamera(

45,

innerWidth/
innerHeight,

0.1,

100

);



camera.position.z=3.5;






renderer =
new THREE.WebGLRenderer({

antialias:true

});



renderer.setPixelRatio(
devicePixelRatio
);



renderer.setSize(

innerWidth,

innerHeight

);



document.body.appendChild(
renderer.domElement
);






scene.add(

new THREE.AmbientLight(

0xffffff,

1.5

)

);





earth =
createEarth(
scene
);







controls =
new OrbitControls(

camera,

renderer.domElement

);



controls.enableDamping=true;


controls.enablePan=false;


controls.minDistance=2;


controls.maxDistance=6;






bindButtons();


animate();



}









function bindButtons(){


document

.querySelectorAll(
"button"
)

.forEach(btn=>{


btn.onclick=()=>{


focusCity(

btn.dataset.city

);


};



});



}









function focusCity(name){


let city =
cities[name];


if(!city)
return;





let pos =

latLonToVector3(

city.lon,

city.lat

);





autoRotate=false;






/*
 地球旋转到目标
*/


smoothRotateTo(
pos
);






/*
 创建光柱

*/


createLocationEffect(

earth,

pos,

name

);







/*
 相机靠近

*/


zoomCamera(
2.7
);



showHUD(

name,

city.zone

);






setTimeout(()=>{


zoomCamera(
3.5
);



},3000);






setTimeout(()=>{


autoRotate=true;



},6000);



}









function smoothRotateTo(pos){



let start =
earth.rotation.y;



let target =

-Math.atan2(

pos.x,

pos.z

);



let duration=1800;



let begin =
performance.now();



function animateRotate(now){



let t=

(now-begin)
/duration;



if(t>1)
t=1;



// smoothstep

t=
t*t*(3-2*t);




earth.rotation.y=

start+

(target-start)*t;




if(t<1)

requestAnimationFrame(
animateRotate
);



}



requestAnimationFrame(
animateRotate
);



}









function zoomCamera(targetZ){



let startZ=

camera.position.z;



let start=

performance.now();



let duration=1000;



function move(now){



let t=

(now-start)
/duration;



if(t>1)
t=1;



t=
t*t*(3-2*t);



camera.position.z=

startZ+

(targetZ-startZ)*t;




if(t<1)

requestAnimationFrame(
move
);



}



requestAnimationFrame(
move
);



}









function showHUD(
name,
zone
){



let box=

document.getElementById(
"locationHUD"
);



if(!box)
return;




document.getElementById(

"locationName"

)

.innerHTML=name;



document.getElementById(

"locationZone"

)

.innerHTML=zone;



box.style.opacity=1;




setTimeout(()=>{


box.style.opacity=0;


},4000);



}









function updateTime(){


let now =
new Date();



let el =
document.getElementById(
"time"
);



if(el)

el.innerHTML=

now.toLocaleTimeString();



}



setInterval(

updateTime,

1000

);



updateTime();









function animate(){



requestAnimationFrame(
animate
);



let t=

clock.getElapsedTime();





if(autoRotate){


rotateEarth(

0.0008

);


}






updateEffects(t);




controls.update();



renderer.render(

scene,

camera

);



}









window.onresize=()=>{


camera.aspect=

innerWidth/

innerHeight;



camera.updateProjectionMatrix();



renderer.setSize(

innerWidth,

innerHeight

);



};