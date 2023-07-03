/**HEADER*******************************************************************
  project : VdMot Controller

  author : SurfGargano, Lenti84

  Comments:

  Version :

  Modifcations :


***************************************************************************
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESSED OR
* IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
* IN NO EVENT SHALL THE DEVELOPER OR ANY CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
* INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
* HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
* IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
* THE POSSIBILITY OF SUCH DAMAGE.
*
**************************************************************************
  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License.
  See the GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.

  Copyright (C) 2021 Lenti84  https://github.com/Lenti84/VdMot_Controller

*END************************************************************************/



#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <Syslog.h>
#include <string.h>
#include "mqtt.h"
#include "globals.h"
#include "stmApp.h"
#include "VdmNet.h"
#include "VdmConfig.h"
#include "VdmSystem.h"
#include "VdmTask.h"
#include "helper.h"
#include "web.h"
#include "PIControl.h"
#include "Messenger.h"


CMqtt Mqtt;

void mcallback(char* topic, byte* payload, unsigned int length) 
{
    Mqtt.callback(topic, payload, length);
}


WiFiClient espClient;
PubSubClient mqtt_client(espClient);

void CMqtt::mqtt_setup(IPAddress brokerIP,uint16_t brokerPort) 
{
    memset(lastValveValues,0x0,sizeof(lastValveValues)); 
    for (uint8_t i=0;i<TEMP_SENSORS_COUNT;i++) {
        lastTempValues[i].temperature=0;
        memset(lastTempValues[i].id,0x0,sizeof(lastTempValues[i].id));
        lastTempValues[i].publishNow=false;
    }
    messengerSend=false;
    mqttReceived=false;
    mqtt_client.setServer(brokerIP, brokerPort);
    mqtt_client.setCallback(mcallback);

    memset (mqtt_mainTopic,0,sizeof(mqtt_mainTopic));
    if (VdmConfig.configFlash.protConfig.protocolFlags.publishPathAsRoot) strncpy(mqtt_mainTopic,"/",sizeof(mqtt_mainTopic));

    if (strlen(VdmConfig.configFlash.systemConfig.stationName)>0) {
        strncat(mqtt_mainTopic, VdmConfig.configFlash.systemConfig.stationName,sizeof(mqtt_mainTopic) - strlen (mqtt_mainTopic) - 1);
        strncat(mqtt_mainTopic, "/",sizeof(mqtt_mainTopic) - strlen (mqtt_mainTopic) - 1);
    } else  {
        strncat(mqtt_mainTopic, DEFAULT_MAINTOPIC,sizeof(mqtt_mainTopic) - strlen (mqtt_mainTopic) - 1);
    }

    strncpy(mqtt_commonTopic, mqtt_mainTopic, sizeof(mqtt_commonTopic));
    strncat(mqtt_commonTopic, DEFAULT_COMMONTOPIC,sizeof(mqtt_commonTopic) - strlen (mqtt_commonTopic) - 1);
    strncpy(mqtt_valvesTopic, mqtt_mainTopic,sizeof(mqtt_commonTopic));
    strncat(mqtt_valvesTopic, DEFAULT_VALVESTOPIC,sizeof(mqtt_valvesTopic)- strlen (mqtt_valvesTopic) - 1);
    strncpy(mqtt_tempsTopic, mqtt_mainTopic,sizeof(mqtt_commonTopic));
    strncat(mqtt_tempsTopic, DEFAULT_TEMPSTOPIC,sizeof(mqtt_tempsTopic)- strlen (mqtt_tempsTopic) - 1);

    if (strlen(VdmConfig.configFlash.systemConfig.stationName)>0) {
        strncpy(stationName, VdmConfig.configFlash.systemConfig.stationName,sizeof(stationName));
    } else strncpy(stationName, DEVICE_HOSTNAME,sizeof(stationName));

}

CMqtt::CMqtt()
{
       
}

uint8_t CMqtt::checkForPublish() 
{
    uint8_t result=publishNothing;
    uint8_t i;

    forcePublish = true;
    if (firstPublish) {
        return publishCommon+publishValves+publishTemps;    
    }
    if (!VdmConfig.configFlash.protConfig.protocolFlags.publishOnChange) {
        if ((millis()-tsPublish)>(1000*VdmConfig.configFlash.protConfig.publishInterval)) {
            return publishCommon+publishValves+publishTemps;    
        }
    } else {
        forcePublish = false;
        if ((millis()-tsPublish)>(1000*VdmConfig.configFlash.protConfig.minBrokerDelay)) {
            // common
            if (VdmSystem.systemMessage.length()>0) result = publishCommon; 
            if (lastCommonValues.heatControl!=VdmConfig.configFlash.valvesControlConfig.heatControl) result = publishCommon; 
            if (lastCommonValues.parkingPosition!=VdmConfig.configFlash.valvesControlConfig.parkingPosition) result = publishCommon; 
            if (lastCommonValues.systemState!=VdmSystem.systemState) result = publishCommon; 
        }
        // valves
        for (i=0;i<ACTUATOR_COUNT;i++) {
            lastValveValues[i].publishNow=false;
            if (VdmConfig.configFlash.valvesConfig.valveConfig[i].active) {
                if ((millis()-lastValveValues[i].ts)>(1000*VdmConfig.configFlash.protConfig.minBrokerDelay)) {
                    if ((lastValveValues[i].position!=StmApp.actuators[i].actual_position) || 
                        (lastValveValues[i].target!=StmApp.actuators[i].target_position) ||
                        (lastValveValues[i].state!=StmApp.actuators[i].state) ||
                        (lastValveValues[i].meanCurrent!=StmApp.actuators[i].meancurrent) ||
                        (lastValveValues[i].temp1!=StmApp.actuators[i].temp1) ||
                        (lastValveValues[i].temp2!=StmApp.actuators[i].temp2) ||
                        ((millis()-lastValveValues[i].ts)>(1000*VdmConfig.configFlash.protConfig.publishInterval))) {
                            result|=publishValves;
                            lastValveValues[i].publishNow=true;
                            if ((millis()-lastValveValues[i].ts)>(1000*VdmConfig.configFlash.protConfig.publishInterval))
                                lastValveValues[i].publishTimeOut=true;
                    }
                }
            }
        } 
        // temps
        for (i=0;i<TEMP_SENSORS_COUNT;i++) {
            lastTempValues[i].publishNow=false;
            if (VdmConfig.configFlash.tempsConfig.tempConfig[i].active) {
                if ((millis()-lastTempValues[i].ts)>(1000*VdmConfig.configFlash.protConfig.minBrokerDelay)) {
                    if ((lastTempValues[i].temperature!=StmApp.temps[i].temperature) ||
                        ((millis()-lastTempValues[i].ts)>(1000*VdmConfig.configFlash.protConfig.publishInterval))) {
                            result|=publishTemps;
                            lastTempValues[i].publishNow=true;
                    } 
                }
            }
        }
    }
    return result;
}

void CMqtt::mqtt_loop() 
{
    if (!mqtt_client.connected()) {
        firstPublish=true;
        reconnect();        
    }
    if (mqtt_client.connected()) {
        mqtt_client.loop();
        if (VdmConfig.configFlash.protConfig.publishInterval<2) VdmConfig.configFlash.protConfig.publishInterval=2;
        uint8_t check=checkForPublish();
       
        if (check!=0) { 
            tsPublish = millis();
            firstPublish=false;
            publish_all(check);
        }
        messengerSend=false;
        connectTimeout=millis();
    } else {
        if ((VdmConfig.configFlash.messengerConfig.reason.reasonFlags.mqttTimeOut) && (!messengerSend)){
            if ((millis()-connectTimeout)>((uint32_t)60*1000*VdmConfig.configFlash.messengerConfig.reason.mqttTimeOutTime)) {
                String title = String(VdmConfig.configFlash.systemConfig.stationName) + " : MQTT" ;
                String s = "MQTT connect failed after "+String(VdmConfig.configFlash.messengerConfig.reason.mqttTimeOutTime)+" minutes";
                Messenger.sendMessage (title.c_str(),s.c_str());
                messengerSend=true;
            }  
        }
    }
        
    mqttConnected=mqtt_client.connected();
    mqttState=mqtt_client.state();
    /*
        -4 : MQTT_CONNECTION_TIMEOUT - the server didn't respond within the keepalive time
        -3 : MQTT_CONNECTION_LOST - the network connection was broken
        -2 : MQTT_CONNECT_FAILED - the network connection failed
        -1 : MQTT_DISCONNECTED - the client is disconnected cleanly
         0 : MQTT_CONNECTED - the client is connected
         1 : MQTT_CONNECT_BAD_PROTOCOL - the server doesn't support the requested version of MQTT
         2 : MQTT_CONNECT_BAD_CLIENT_ID - the server rejected the client identifier
         3 : MQTT_CONNECT_UNAVAILABLE - the server was unable to accept the connection
         4 : MQTT_CONNECT_BAD_CREDENTIALS - the username/password were rejected
         5 : MQTT_CONNECT_UNAUTHORIZED - the client was not authorized to connect
    */
}


void CMqtt::reconnect() 
{
    char topicstr[MAINTOPIC_LEN+50];
    char nrstr[11];
    char* mqttUser = NULL;
    char* mqttPwd = NULL;
    uint8_t len;
    tsPublish = millis();
    if ((strlen(VdmConfig.configFlash.protConfig.userName)>0) && (strlen(VdmConfig.configFlash.protConfig.userPwd)>0)) {
        mqttUser = VdmConfig.configFlash.protConfig.userName;
        mqttPwd = VdmConfig.configFlash.protConfig.userPwd;
    }
    if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_ON) {
        syslog.log(LOG_DEBUG, "MQTT reconnecting ...");
    }
    #ifdef EnvDevelop
        UART_DBG.println("Reconnecting MQTT...");
    #endif
    
    mqtt_client.setKeepAlive(VdmConfig.configFlash.protConfig.keepAliveTime);
    mqtt_client.setSocketTimeout(VdmConfig.configFlash.protConfig.keepAliveTime);

    if (!mqtt_client.connect(stationName,mqttUser,mqttPwd)) {
        #ifdef EnvDevelop
            UART_DBG.print("failed, rc=");
            UART_DBG.print(mqtt_client.state());
            UART_DBG.println(" retrying");
        #endif
        if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_ON) {
            syslog.log(LOG_ERR, "MQTT failed rc="+String(mqtt_client.state())+String(" retrying"));
        }
        mqttState=mqtt_client.state();
        VdmTask.yieldTask(5000);
        return;
    }
    
    mqtt_client.setKeepAlive(VdmConfig.configFlash.protConfig.keepAliveTime);
    mqtt_client.setSocketTimeout(VdmConfig.configFlash.protConfig.keepAliveTime);

    // make some subscriptions
    memset(topicstr,0x0,sizeof(topicstr));
    strncat(topicstr,mqtt_commonTopic,sizeof(topicstr) - strlen (topicstr) - 1);
    len = strlen(topicstr);
    strncat(topicstr, "heatControl",sizeof(topicstr) - strlen (topicstr) - 1);
    mqtt_client.subscribe(topicstr);    
    topicstr[len] = '\0';
    strncat(topicstr, "parkPosition",sizeof(topicstr) - strlen (topicstr) - 1);
    mqtt_client.subscribe(topicstr);    

    for (uint8_t x = 0;x<ACTUATOR_COUNT;x++) {
        lastValveValues[x].ts=millis();
        lastValveValues[x].publishNow=false;
        lastValveValues[x].publishTimeOut=false;
        if (VdmConfig.configFlash.valvesConfig.valveConfig[x].active) {
            memset(topicstr,0x0,sizeof(topicstr));
            memset(nrstr,0x0,sizeof(nrstr));
            itoa((x+1), nrstr, 10);
            if (strlen(VdmConfig.configFlash.valvesConfig.valveConfig[x].name)>0)
                strncpy(nrstr,VdmConfig.configFlash.valvesConfig.valveConfig[x].name,sizeof(nrstr));
            // prepare prefix
            strncat(topicstr, mqtt_valvesTopic,sizeof(topicstr) - strlen (topicstr) - 1);
            strncat(topicstr, nrstr,sizeof(topicstr) - strlen (topicstr) - 1);      
            len = strlen(topicstr);

            // target value
            strncat(topicstr, "/target",sizeof(topicstr) - strlen (topicstr) - 1);
            mqtt_client.subscribe(topicstr);

            if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[x].controlFlags.active) {
                if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[x].link==0) {
                    if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[x].valueSource==0) {
                        // temp value
                        topicstr[len] = '\0';
                        strncat(topicstr, "/tValue",sizeof(topicstr) - strlen (topicstr) - 1);
                        mqtt_client.subscribe(topicstr);    
                    } 
                    if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[x].targetSource==0) {
                        // temp target
                        topicstr[len] = '\0';
                        strncat(topicstr, "/tTarget",sizeof(topicstr) - strlen (topicstr) - 1);
                        mqtt_client.subscribe(topicstr);    
                    } 
                }
            }
        }
    }
    for (uint8_t x = 0;x<TEMP_SENSORS_COUNT;x++) {
        lastTempValues[x].ts=millis();
        lastTempValues[x].publishNow=false;
    }
    if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_ON) {
        syslog.log(LOG_DEBUG, "MQTT Connected...");
    }
   
}

void CMqtt::callback(char* topic, byte* payload, unsigned int length) 
{
    bool found;
    char item[20];
    char* pt;
    uint8_t i;
    uint8_t idx;
    uint8_t val8;

    if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
               syslog.log(LOG_DEBUG, "MQTT: callback "+String(topic));
    }
    if (length>0) {
        if (!VdmConfig.configFlash.protConfig.protocolFlags.publishPathAsRoot) {
            if (topic[0]=='/') {
                topic++;        // adjust topic 
            }
        }

        // local zero terminated copy of payload
        char value[32] = {0};
        memcpy(value, payload, std::min<size_t>(sizeof(value) - 1, length));

        if (memcmp(mqtt_commonTopic,(const char*) topic, strlen(mqtt_commonTopic))==0) {
            memset(item,0x0,sizeof(item));
            pt= (char*) topic;
            pt+= strlen(mqtt_commonTopic);
            if (strncmp(pt,"heatControl",sizeof("heatControl"))==0) {
                val8=atoi(value);
                if (val8!=VdmConfig.configFlash.valvesControlConfig.heatControl) {
                    VdmConfig.configFlash.valvesControlConfig.heatControl=val8;
                    VdmConfig.writeValvesControlConfig(false,false); 
                }
            } 
            if (strncmp(pt,"parkPosition",sizeof("parkPosition"))==0) {
                val8=atoi(value); 
                if (val8!=VdmConfig.configFlash.valvesControlConfig.heatControl) {
                    VdmConfig.configFlash.valvesControlConfig.parkingPosition=val8;
                    VdmConfig.writeValvesControlConfig(false,false); 
                }
            }    
        }

        else if (memcmp(mqtt_valvesTopic,(const char*) topic, strlen(mqtt_valvesTopic))==0) {
            memset(item,0x0,sizeof(item));
            pt= (char*) topic;
            pt+= strlen(mqtt_valvesTopic);
            idx=0;
            for (i=strlen(mqtt_valvesTopic);i<strlen(topic);i++) {
                if (*pt=='/') break;
                item[idx]=*pt;
                idx++;
                pt++;
            } 
            
            // find approbiated valve
            idx=0;
            found = false;
            for (i=0;i<ACTUATOR_COUNT;i++) {
                if (strncmp(VdmConfig.configFlash.valvesConfig.valveConfig[i].name,item,sizeof(VdmConfig.configFlash.valvesConfig.valveConfig[i].name))==0) {
                    found = true;
                    break;
                }
                idx++;    
            }
            if (!found) {
                if (isNumber(item)) {
                    idx=atoi(item)-1;
                    found=true;
                }
            }

            if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
               syslog.log(LOG_DEBUG, "MQTT: payload "+String(topic)+" : "+String(value));
            }  
            if (found) {
               
                if (isFloat(value)) {
                    if (strncmp(pt,"/target",7)==0) {
                        if (VdmConfig.configFlash.valvesConfig.valveConfig[idx].active) {
                            StmApp.actuators[idx].target_position = atoi(value);
                        }
                    } else if (strncmp(pt,"/tValue",7)==0) {
                        if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[idx].controlFlags.active) {
                            if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[idx].valueSource==0) {
                                PiControl[idx].value=strtof(value, NULL);
                                mqttReceived=true;
                            }
                        }
                    } else if (strncmp(pt,"/tTarget",8)==0) {
                        if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[idx].controlFlags.active) {
                            if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[idx].targetSource==0) {
                                PiControl[idx].target=strtof(value, NULL);
                                mqttReceived=true;
                            }
                        }
                    }else if (strncmp(pt,"/dynOffs",8)==0) {
                        if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[idx].controlFlags.active) {
                            if (VdmConfig.configFlash.valvesControlConfig.valveControlConfig[idx].targetSource==0)
                                PiControl[idx].dynOffset=atoi(value);
                        }
                    }
                    if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                        syslog.log(LOG_DEBUG, "MQTT: found topic "+String(item)+String(pt)+" : "+String(value));
                    }
                } else {
                    if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                        syslog.log(LOG_DEBUG, "MQTT: found topic, but not a number "+String(item)+String(pt)+" : "+String(value));
                    }  
                }
            } else {
                if (VdmConfig.configFlash.netConfig.syslogLevel>=VISMODE_DETAIL) {
                    syslog.log(LOG_DEBUG, "MQTT: not found topic "+String(item));
                }   
            }
            
        }
    }
}

void CMqtt::publish_valves () {
    char topicstr[MAINTOPIC_LEN+30];
    char nrstr[11];
    char valstr[10];
    int8_t tempIdx;
    uint8_t len;

    for (uint8_t x = 0;x<ACTUATOR_COUNT;x++) {
        if (VdmConfig.configFlash.valvesConfig.valveConfig[x].active) {
            if (lastValveValues[x].publishNow || forcePublish) {
                memset(topicstr,0x0,sizeof(topicstr));
                memset(nrstr,0x0,sizeof(nrstr));
                itoa((x+1), nrstr, 10);
                if (strlen(VdmConfig.configFlash.valvesConfig.valveConfig[x].name)>0)
                    strncpy(nrstr,VdmConfig.configFlash.valvesConfig.valveConfig[x].name,sizeof(nrstr));
                // prepare prefix
                strncat(topicstr, mqtt_valvesTopic,sizeof(topicstr) - strlen (topicstr) - 1);
                strncat(topicstr, nrstr,sizeof(topicstr) - strlen (topicstr) - 1);
                len = strlen(topicstr);

                // actual value
                if ((lastValveValues[x].position!=StmApp.actuators[x].actual_position) || forcePublish || lastValveValues[x].publishTimeOut) {
                    strncat(topicstr, "/actual",sizeof(topicstr) - strlen (topicstr) - 1);
                    itoa(StmApp.actuators[x].actual_position, valstr, 10);        
                    mqtt_client.publish(topicstr, valstr);
                    lastValveValues[x].position=StmApp.actuators[x].actual_position;
                }
                // target
                if (VdmConfig.configFlash.protConfig.protocolFlags.publishTarget)  {
                    if ((lastValveValues[x].target!=StmApp.actuators[x].target_position) || forcePublish || lastValveValues[x].publishTimeOut) {
                        topicstr[len] = '\0';
                        strncat(topicstr, "/target",sizeof(topicstr) - strlen (topicstr) - 1);
                        itoa(StmApp.actuators[x].target_position, valstr, 10);
                        mqtt_client.publish(topicstr, valstr);
                        lastValveValues[x].target=StmApp.actuators[x].target_position;
                    }
                }
                // state
                if ((lastValveValues[x].state!=StmApp.actuators[x].state) || forcePublish || lastValveValues[x].publishTimeOut) {
                    topicstr[len] = '\0';
                    strncat(topicstr, "/state",sizeof(topicstr) - strlen (topicstr) - 1);
                    itoa(StmApp.actuators[x].state, valstr, 10);
                    mqtt_client.publish(topicstr, valstr);
                    lastValveValues[x].state=StmApp.actuators[x].state;
                }
                // meancurrent
                if ((lastValveValues[x].meanCurrent!=StmApp.actuators[x].meancurrent) || forcePublish || lastValveValues[x].publishTimeOut) {
                    topicstr[len] = '\0';
                    strncat(topicstr, "/meancur",sizeof(topicstr) - strlen (topicstr) - 1);
                    itoa(StmApp.actuators[x].meancurrent, valstr, 10);
                    mqtt_client.publish(topicstr, valstr);
                    lastValveValues[x].meanCurrent=StmApp.actuators[x].meancurrent;
                }
                // temperature 1st sensor
                if ((lastValveValues[x].temp1!=StmApp.actuators[x].temp1) || forcePublish || lastValveValues[x].publishTimeOut) {
                    topicstr[len] = '\0';
                    strncat(topicstr, "/temp1",sizeof(topicstr) - strlen (topicstr) - 1);
                    String s = String(((float)StmApp.actuators[x].temp1)/10,1); 
                    mqtt_client.publish(topicstr, (const char*) &s);
                    lastValveValues[x].temp1=StmApp.actuators[x].temp1;
                }
                // temperature 2nd sensor
                if ((lastValveValues[x].temp2!=StmApp.actuators[x].temp2) || forcePublish || lastValveValues[x].publishTimeOut) {
                    topicstr[len] = '\0';
                    strncat(topicstr, "/temp2",sizeof(topicstr) - strlen (topicstr) - 1);
                    String s = String(((float)StmApp.actuators[x].temp2)/10,1); 
                    mqtt_client.publish(topicstr, (const char*) &s);
                    lastValveValues[x].temp2=StmApp.actuators[x].temp2;
                }
                lastValveValues[x].publishNow=false;
                lastValveValues[x].publishTimeOut=false;
                lastValveValues[x].ts=millis();
            }
        }
    }
}

void CMqtt::publish_temps()
{
    char topicstr[MAINTOPIC_LEN+30];
    char nrstr[11];
    int8_t tempIdx;
    uint8_t len;
    
    for (uint8_t x = 0;x<StmApp.tempsCount;x++) {
        if (lastTempValues[x].publishNow || forcePublish) {
            tempIdx=VdmConfig.findTempID(StmApp.temps[x].id);
            if (tempIdx>=0) {
                if (VdmConfig.configFlash.tempsConfig.tempConfig[tempIdx].active) {
                    if ((!Web.findIdInValve (tempIdx)) || VdmConfig.configFlash.protConfig.protocolFlags.publishAllTemps) {
                        memset(topicstr,0x0,sizeof(topicstr));
                        memset(nrstr,0x0,sizeof(nrstr));
                        itoa((x+1), nrstr, 10);
                        if (strlen(VdmConfig.configFlash.tempsConfig.tempConfig[tempIdx].name)>0)
                            strncpy(nrstr,VdmConfig.configFlash.tempsConfig.tempConfig[tempIdx].name,sizeof(nrstr));
                        // prepare prefix
                        strncat(topicstr, mqtt_tempsTopic,sizeof(topicstr) - strlen (topicstr) - 1);
                        strncat(topicstr, nrstr,sizeof(topicstr) - strlen (topicstr) - 1);
                        len = strlen(topicstr);
                        // id
                        strncat(topicstr, "/id",sizeof(topicstr) - strlen (topicstr) - 1);     
                        mqtt_client.publish(topicstr,StmApp.temps[x].id);
                        // actual value
                        topicstr[len] = '\0';
                        strncat(topicstr, "/value",sizeof(topicstr) - strlen (topicstr) - 1);
                        String s = String(((float)StmApp.temps[x].temperature)/10,1);     
                        mqtt_client.publish(topicstr,(const char*) &s);
                    }
                }
            }
            lastTempValues[x].temperature=StmApp.temps[x].temperature;
            lastTempValues[x].publishNow=false;
            lastTempValues[x].ts=millis();
        }
    }
} 

void CMqtt::publish_common () 
{
    char topicstr[MAINTOPIC_LEN+30];
    char nrstr[11];
    char valstr[10];
    int8_t tempIdx;
    uint8_t len;
    
    memset(topicstr,0x0,sizeof(topicstr));
    strncat(topicstr,mqtt_commonTopic,sizeof(topicstr) - strlen (topicstr) - 1);
    len = strlen(topicstr);
    if ((!VdmConfig.configFlash.protConfig.protocolFlags.publishOnChange) || (lastCommonValues.heatControl!=VdmConfig.configFlash.valvesControlConfig.heatControl)) {
        strncat(topicstr, "heatControl",sizeof(topicstr) - strlen (topicstr) - 1);
        itoa(VdmConfig.configFlash.valvesControlConfig.heatControl, valstr, 10);        
        mqtt_client.publish(topicstr, valstr); 
        lastCommonValues.heatControl=VdmConfig.configFlash.valvesControlConfig.heatControl; 
    }

    topicstr[len] = '\0';
    if ((!VdmConfig.configFlash.protConfig.protocolFlags.publishOnChange) || (lastCommonValues.parkingPosition!=VdmConfig.configFlash.valvesControlConfig.parkingPosition)) {
        strncat(topicstr, "parkPosition",sizeof(topicstr) - strlen (topicstr) - 1);
        itoa(VdmConfig.configFlash.valvesControlConfig.parkingPosition, valstr, 10);        
        mqtt_client.publish(topicstr, valstr);  
        lastCommonValues.parkingPosition=VdmConfig.configFlash.valvesControlConfig.parkingPosition; 
    }
    topicstr[len] = '\0';
    
    if ((!VdmConfig.configFlash.protConfig.protocolFlags.publishOnChange) || (lastCommonValues.systemState!=VdmSystem.systemState)) {
        strncat(topicstr, "state",sizeof(topicstr) - strlen (topicstr) - 1);
        itoa(VdmSystem.systemState , valstr, 10);        
        mqtt_client.publish(topicstr, valstr);  
        lastCommonValues.systemState=VdmSystem.systemState;
    }
    topicstr[len] = '\0';
    if (VdmConfig.configFlash.protConfig.protocolFlags.publishUpTime) {
        strncat(topicstr, "uptime",sizeof(topicstr) - strlen (topicstr) - 1);
        String upTime = VdmSystem.getUpTime();
        mqtt_client.publish(topicstr, (const char*) (upTime.c_str())); 
    }
    
    topicstr[len] = '\0';
    if (VdmSystem.systemMessage.length()>0) {
        topicstr[len] = '\0';
        strncat(topicstr, "message",sizeof(topicstr) - strlen (topicstr) - 1);       
        mqtt_client.publish(topicstr,VdmSystem.systemMessage.c_str());  
        VdmSystem.systemMessage=""; 
    }
}
    
void CMqtt::publish_all (uint8_t publishFlags) 
{
   if (CHECK_BIT(publishFlags,0)==1) publish_common (); 
   if (CHECK_BIT(publishFlags,1)==1) publish_valves ();
   if (CHECK_BIT(publishFlags,2)==1) publish_temps ();
}