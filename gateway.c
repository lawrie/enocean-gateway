#include "EOLink/eoLink.h"
#include <stdio.h>
#include "stdlib.h"
#include "string.h"
#include <unistd.h>
#include "MQTTClient.h"

#define SER_PORT    "/dev/ttyAMA0"
#define ADDRESS     "tcp://192.168.0.101:1883"
#define CLIENTID    "enocean"
#define BUTTON_TOPIC       "/house/rooms/first-floor/living-room/button"
#define CONTACT_TOPIC      "/house/rooms/first-floor/living-room/middle-switch"
#define TEMPERATURE_TOPIC  "/house/rooms/second-floor/bathroom/temperature"
 
#define QOS         1
#define TIMEOUT     10000L

MQTTClient client;
MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
MQTTClient_message pubmsg = MQTTClient_message_initializer;
MQTTClient_deliveryToken token;
const char* topic;

void init() {
    MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession = 1;

    pubmsg.qos = QOS;
    pubmsg.retained = 0;
}
 
void connect()
{
    int rc;

    if ((rc = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS)
    {
        printf("Failed to connect, return code %d\n", rc);
        exit(-1);
    }
}

int publish(const char *topic, char* msg)
{
     pubmsg.payload = msg;
     pubmsg.payloadlen = strlen(msg);

     return MQTTClient_publishMessage(client, topic, &pubmsg, &token);
}

void gateway()
{
	eoGateway gateway;
	uint16_t recv;
        char msg[12];

	if (gateway.Open(SER_PORT)!=EO_OK)
	{
		printf("Failed to open serial port\n");
		return ;
	}

	printf("EnOcean-Link Gateway\n");

	//we set now the automatic RPS-Teach-IN information
	gateway.TeachInModule->SetRPS(0x02,0x01);
	//Activate LearnMode
	gateway.LearnMode=true;
	while (1)
	{
		//eoGateway is normally in LearnMode, to unset LearnMode, set LearnMode to false
		//gateway.LearnMode=false;
		recv = gateway.Receive();

		if (recv & RECV_TELEGRAM)
		{
			unsigned int sourceID = gateway.telegram.sourceID;
                        int d;
                        int dl = gateway.telegram.dataLength;
                        unsigned char *data = gateway.telegram.data;
			printf("Read telegram\n");
			printf("Data length: %d\n", dl);
                        if (dl == 4) d = ((255 - data[2]) / 256) * 400;
                        else d = data[0];
			printf("Data: %d\n",d);
			printf("Source ID: %x\n", sourceID);
			eoDebug::Print(gateway.telegram);
			if (sourceID == 0x1812e18) 
			{
				d = (d -9) * -1;
			}
                        sprintf(msg,"%d", d);
			printf("msg: %s\n", msg);
                        if (sourceID == 0x294a0c) topic = BUTTON_TOPIC;
                        else if (sourceID == 0x1812e18) topic = CONTACT_TOPIC;
			else topic = TEMPERATURE_TOPIC;
                        int rc = publish(topic, msg);
			printf("publish result: %d\n", rc);
			if (rc != MQTTCLIENT_SUCCESS) 
			{
				connect();
				publish(topic, msg);
			}
		}
		else if (recv & RECV_PACKET) {
			printf("Read packet\n");
			eoDebug::Print(gateway.packet);
		}

		if ((recv & RECV_TEACHIN))
		{
			printf("Receive teachin\n");
			eoProfile *profile = gateway.device->GetProfile();
			printf("Device %08X Learned-In EEP: %02X-%02X-%02X\n", gateway.device->ID, profile->rorg, profile->func, profile->type );

			for (int i = 0; i<profile->GetChannelCount(); i++)
			{
				printf("%s %.2f ... %.2f %s\n", profile->GetChannel(i)->ToString(NAME), profile->GetChannel(i)->min, profile->GetChannel(i)->max, profile->GetChannel(i)->ToString(UNIT));
			}

		}

		if (recv & RECV_PROFILE)
		{
			printf("Received profile\n");
			printf("Device %08X\n", gateway.device->ID);
			eoProfile *profile = gateway.device->GetProfile();

			float f;
			uint8_t t;
			for (int i = 0; i<profile->GetChannelCount(); i++)
			{
				if (profile->GetValue( profile->GetChannel(i)->type, f) ==EO_OK)
					printf("%s %.2f %s\n", profile->GetChannel(i)->ToString(NAME),f,profile->GetChannel(i)->ToString(UNIT));
				if (profile->GetValue( profile->GetChannel(i)->type, t) ==EO_OK)
									printf("%s %u \n", profile->GetChannel(i)->ToString(NAME),t);
			}

		}

	}

}

int main(int argc, char* argv[])
{
    init();
    connect();
    gateway();
}
