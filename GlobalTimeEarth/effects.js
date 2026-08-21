import * as THREE from "three";


let activePoint = null;
let waves = [];

let startTime = 0;



export function createLocationEffect(
    earth,
    position,
    name
){

    clearEffect(earth);



    let normal =
    position.clone()
    .normalize();



    /*
        白色定位点
    */

    activePoint = new THREE.Mesh(

        new THREE.SphereGeometry(
            0.018,
            32,
            32
        ),

        new THREE.MeshBasicMaterial({

            color:0xffffff,

            transparent:true,

            opacity:0

        })

    );


    activePoint.position.copy(
        position
    );


    earth.add(
        activePoint
    );




    /*
        创建3层扩散水波
    */


    for(let i=0;i<3;i++){


        let ring =

        new THREE.Mesh(

            new THREE.RingGeometry(

                0.03,
                0.045,
                64

            ),


            new THREE.MeshBasicMaterial({

                color:0xffffff,

                transparent:true,

                opacity:0,

                side:
                THREE.DoubleSide

            })

        );



        ring.position.copy(

            position.clone()
            .multiplyScalar(1.002)

        );



        /*
          Ring默认Z轴朝向
          对齐球面法线
        */

        ring.quaternion

        .setFromUnitVectors(

            new THREE.Vector3(
                0,
                0,
                1
            ),

            normal

        );



        ring.userData={
            delay:i*0.8
        };



        earth.add(ring);


        waves.push(ring);


    }



    startTime=
    performance.now();


}








export function updateEffects(time){


    if(activePoint){


        let t=

        (performance.now()
        -
        startTime)
        /
        600;



        if(t>1)
        t=1;



        activePoint.material.opacity=t;



    }





    waves.forEach(w=>{


        let t=

        (
            performance.now()
            -
            startTime
            )
            /
            1800;



        t-=w.userData.delay;



        if(t<0)
        return;



        if(t>1)
        t=t%1;



        let scale=

        1+
        t*5;



        w.scale.set(

            scale,

            scale,

            scale

        );



        w.material.opacity=

        (1-t)*0.6;



    });



}









function clearEffect(earth){



    if(activePoint){

        earth.remove(
            activePoint
        );

        activePoint=null;

    }



    waves.forEach(w=>{

        earth.remove(w);

    });


    waves=[];


}