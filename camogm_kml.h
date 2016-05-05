int camogm_init_kml(void);
int camogm_start_kml(void);
int camogm_frame_kml(void);
int camogm_end_kml(void);
void camogm_free_kml(void);











/*
   <?xml version="1.0" encoding="UTF-8"?>
   <kml xmlns="http://earth.google.com/kml/2.2">
   <Document>



   <PhotoOverlay>

      <TimeStamp>
        <when>2007-01-14T21:05:02Z</when>
      </TimeStamp>

   <!-- inherited from Feature element -->
   <name>...</name>                      <!-- string -->
   <visibility>1</visibility>            <!-- boolean -->
   <open>0</open>                        <!-- boolean -->
   <atom:author>...<atom:author>         <!-- xmlns:atom -->
   <atom:link>...</atom:link>            <!-- xmlns:atom -->
   <address>...</address>                <!-- string -->
   <AddressDetails xmlns="urn:oasis:names:tc:ciq:xsdschema:xAL:2.0">...
      </AddressDetails>                 <!-- string -->
   <phoneNumber>...</phoneNumber>        <!-- string -->
   <Snippet maxLines="2">...</Snippet>   <!-- string -->
   <description>...</description>        <!-- string -->
   <AbstractView>...</AbstractView>      <!-- Camera or LookAt -->
   <TimePrimitive>...</TimePrimitive>
   <styleUrl>...</styleUrl>              <!-- anyURI -->
   <StyleSelector>...</StyleSelector>
   <Region>...</Region>
   <ExtendedData>...</ExtendedData>

   <!-- inherited from Overlay element -->
   <color>ffffffff</color>               <!-- kml:color -->
   <drawOrder>0</drawOrder>              <!-- int -->
   <Icon>
    <href>...</href>                    <!-- anyURI -->
    ...
   </Icon>

   <!-- specific to PhotoOverlay -->
   <rotation>0</rotation>                <!-- kml:angle180 -->
   <ViewVolume>
    <leftFov>0</leftFov>                <!-- kml:angle180 -->
    <rightFov>0</rightFov>              <!-- kml:angle180 -->
    <bottomFov>0</bottomFov>            <!-- kml:angle90 -->
    <topFov>0</topFov>                  <!-- kml:angle90 -->
    <near>0</near>                      <!-- double -->
   </ViewVolume>
   <ImagePyramid>
    <tileSize>256</tileSize>            <!-- int -->
    <maxWidth>...</maxWidth>            <!-- int -->
    <maxHeight>...</maxHeight>          <!-- int -->
    <gridOrigin>lowerLeft</gridOrigin>  <!-- lowerLeft or upperLeft-->
   </ImagePyramid>
   <Point>
    <coordinates>...</coordinates>      <!-- lon,lat[,alt] -->
   </Point>
   <shape>rectangle</shape>              <!-- kml:shape -->
   </PhotoOverlay>

   <Camera id="ID">
   <longitude>0</longitude>          <!-- kml:angle180 -->
   <latitude>0</latitude>            <!-- kml:angle90 -->
   <altitude>0</altitude>            <!-- double -->
   <heading>0</heading>              <!-- kml:angle360 -->
   <tilt>0</tilt>                    <!-- kml:anglepos180 -->
   <roll>0</roll>                    <!-- kml:angle180 -->
   <altitudeMode>clampToGround</altitudeMode>
         <!-- kml:altitudeModeEnum: relativeToGround, clampToGround, or absolute -->
   </Camera>
 */
