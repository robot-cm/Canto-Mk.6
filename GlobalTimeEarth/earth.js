import * as THREE from "three";


let earth;
let earthGroup;
let countryLayer;



export function createEarth(scene){


    earthGroup =
    new THREE.Group();


    scene.add(
        earthGroup
    );



    /*
        夜晚渐变地球 Shader
    */


    const material =
    new THREE.ShaderMaterial({

        uniforms:{

            time:{
                value:0
            }

        },


        vertexShader:`

            varying vec3 vNormal;


            void main(){

                vNormal =
                normalize(
                    normalMatrix *
                    normal
                );


                gl_Position =
                projectionMatrix *
                modelViewMatrix *
                vec4(
                    position,
                    1.0
                );

            }

        `,


        fragmentShader:`

            uniform float time;


            varying vec3 vNormal;


            void main(){


                float light =
                dot(
                    vNormal,
                    normalize(
                        vec3(
                            0.5,
                            0.4,
                            1.0
                        )
                    )
                );


                light =
                max(
                    light,
                    0.05
                );


                vec3 day =
                vec3(
                    0.08,
                    0.08,
                    0.08
                );


                vec3 night =
                vec3(
                    0.005,
                    0.005,
                    0.01
                );



                vec3 color =
                mix(
                    night,
                    day,
                    light
                );



                gl_FragColor =
                vec4(
                    color,
                    1.0
                );

            }

        `

    });



    earth =

    new THREE.Mesh(

        new THREE.SphereGeometry(

            1,

            128,

            128

        ),

        material

    );



    earthGroup.add(
        earth
    );




    /*
        边缘微光
    */


    let outline =

    new THREE.Mesh(

        new THREE.SphereGeometry(

            1.005,

            128,

            128

        ),


        new THREE.MeshBasicMaterial({

            color:0xffffff,

            transparent:true,

            opacity:.12,

            wireframe:true

        })

    );



    earthGroup.add(
        outline
    );




    /*
        国家边界层
    */


    countryLayer =
    new THREE.Group();


    earthGroup.add(
        countryLayer
    );



    loadCountries();



    return earthGroup;

}







/*
 经纬度转球面坐标

 */

export function latLonToVector3(

    lon,

    lat,

    radius=1.015

){


    let phi =
    (90-lat)
    *
    Math.PI/180;



    let theta =
    (lon+180)
    *
    Math.PI/180;



    return new THREE.Vector3(

        -radius *
        Math.sin(phi) *
        Math.cos(theta),


        radius *
        Math.cos(phi),


        radius *
        Math.sin(phi) *
        Math.sin(theta)

    );

}







/*
 加载 Natural Earth GeoJSON

 */

async function loadCountries(){


    try{


        let response =

        await fetch(
            "countries.geojson"
        );


        let data =

        await response.json();



        data.features.forEach(

            feature=>{


                let geometry =
                feature.geometry;



                if(
                    geometry.type
                    ===
                    "Polygon"
                ){

                    drawPolygon(
                        geometry.coordinates
                    );

                }



                else if(
                    geometry.type
                    ===
                    "MultiPolygon"
                ){


                    geometry.coordinates
                    .forEach(
                        p=>
                        drawPolygon(p)
                    );


                }


            }

        );


    }

    catch(e){

        console.log(
            "GeoJSON未找到"
        );

    }


}








function drawPolygon(coords){


    coords.forEach(
        ring=>{


            let points=[];


            ring.forEach(
                p=>{


                    points.push(

                        latLonToVector3(

                            p[0],

                            p[1],

                            1.012

                        )

                    );


                }

            );



            let geometry =

            new THREE.BufferGeometry()
            .setFromPoints(
                points
            );



            let line =

            new THREE.Line(

                geometry,


                new THREE.LineBasicMaterial({

                    color:0xffffff,

                    transparent:true,

                    opacity:.5

                })

            );



            countryLayer.add(
                line
            );


        }
    );


}








export function rotateEarth(speed){


    if(earthGroup)

    earthGroup.rotation.y += speed;


}




export function getEarth(){


    return earthGroup;


}