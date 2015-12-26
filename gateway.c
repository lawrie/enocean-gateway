#include "EOLink/eoLink.h"
#include <stdio.h>
#include "stdlib.h"
#include "string.h"
#include <unistd.h>
#include <stdarg.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <fcntl.h>

#include "MQTTClient.h"

#define SER_PORT    "/dev/ttyAMA0"
#define ADDRESS     "tcp://192.168.0.101:1883"
#define CLIENTID    "enocean"
#define BUTTON_TOPIC       "/house/rooms/first-floor/living-room/button"
#define CONTACT_TOPIC      "/house/rooms/ground-floor/hall/letterbox"
#define TEMPERATURE_TOPIC  "/house/rooms/second-floor/bathroom/temperature"
 
#define QOS         1
#define TIMEOUT     10000L

#define TRUE 1
#define FALSE 0

static MQTTClient client;
static MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
static MQTTClient_message pubmsg = MQTTClient_message_initializer;
static MQTTClient_deliveryToken token;
static const char* topic;
static int is_daemon = FALSE;

static const char* PID_FILE = "/tmp/enocean.pid";

// Send an error message to the daemon log or console
static void error(const char *msg, ...)
{
  char str[100];
  va_list args;
  va_start( args, msg );

  vsprintf( str, msg, args );

  va_end( args );

  if (is_daemon) syslog(LOG_NOTICE, str);
  else printf(str);
}

// Log an information message to the daemoon log or console
static void log_msg(const char *msg, ...)
{ 
  char str[100];
  va_list args;
  va_start( args, msg );

  vsprintf( str, msg, args );

  va_end( args );

  if (is_daemon) syslog(LOG_NOTICE, str);
  else printf(str);
}

// Become a daemon
static void become_daemon()
{
  pid_t pid;
  int pidFilehandle;
  char str[10];

  /* Fork off the parent process */
  pid = fork();

  /* An error occurred */
  if (pid < 0)
    exit(EXIT_FAILURE);

  /* Success: Let the parent terminate */
  if (pid > 0)
    exit(EXIT_SUCCESS);

  /* On success: The child process becomes session leader */
  if (setsid() < 0)
    exit(EXIT_FAILURE);

  /* Catch, ignore and handle signals */
  //TODO: Implement a working signal handler */
  signal(SIGCHLD, SIG_IGN);
  signal(SIGHUP, SIG_IGN);

  /* Fork off for the second time*/
  pid = fork();

  /* An error occurred */
  if (pid < 0)
    exit(EXIT_FAILURE);

  /* Success: Let the parent terminate */
  if (pid > 0)
    exit(EXIT_SUCCESS);

  /* Set new file permissions */
  umask(0);

  /* Change the working directory to the root directory */
  /* or another appropriated directory */
  chdir("/");

  /* Close all open file descriptors */
  close(STDIN_FILENO);
  close(STDOUT_FILENO);
  close(STDERR_FILENO);

  /* Open the log file */
  openlog ("enocean", LOG_PID, LOG_DAEMON);
  
  is_daemon = TRUE;

  /* Ensure only one copy */
  pidFilehandle = open(PID_FILE, O_RDWR|O_CREAT, 0600);
 
  if (pidFilehandle == -1 )
  {
    /* Couldn't open lock file */
    log_msg("Could not open PID lock file %s, exiting", PID_FILE);
    exit(EXIT_FAILURE);
  }
  
  /* Try to lock file */
  if (lockf(pidFilehandle,F_TLOCK,0) == -1)
  {
    /* Couldn't get lock on lock file */
    log_msg("Could not lock PID lock file %s, exiting", PID_FILE);
    exit(EXIT_FAILURE);
  }
 
  /* Get and format PID */
  sprintf(str,"%d\n",getpid());
 
  /* write pid to lockfile */
  write(pidFilehandle, str, strlen(str));
}


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
        error("Failed to connect, return code %d\n", rc);
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
		error("Failed to open serial port\n");
		return ;
	}

	log_msg("EnOcean-Link Gateway\n");

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
			
			log_msg("Read telegram\n");
			log_msg("Data length: %d\n", dl);
			
            if (dl == 4) d = ((255 - data[2]) / 256.0) * 400;
            else d = data[0];
			
			log_msg("Data: %d\n",d);
			log_msg("Source ID: %x\n", sourceID);
			eoDebug::Print(gateway.telegram);
			
			if (sourceID == 0x1812e18) 
			{
				d = (d -9) * -1;
			}
            
			sprintf(msg,"%d", d);
			log_msg("msg: %s\n", msg);
			
            if (sourceID == 0x294a0c) topic = BUTTON_TOPIC;
            else if (sourceID == 0x1812e18) topic = CONTACT_TOPIC;
			else topic = TEMPERATURE_TOPIC;
            
			int rc = publish(topic, msg);
			log_msg("publish result: %d\n", rc);
			
			if (rc != MQTTCLIENT_SUCCESS) 
			{
				connect();
				publish(topic, msg);
			}
		}
		else if (recv & RECV_PACKET) {
			log_msg("Read packet\n");
			eoDebug::Print(gateway.packet);
		}

		if ((recv & RECV_TEACHIN))
		{
			log_msg("Receive teachin\n");
			eoProfile *profile = gateway.device->GetProfile();
			log_msg("Device %08X Learned-In EEP: %02X-%02X-%02X\n", gateway.device->ID, profile->rorg, profile->func, profile->type );

			for (int i = 0; i<profile->GetChannelCount(); i++)
			{
				log_msg("%s %.2f ... %.2f %s\n", profile->GetChannel(i)->ToString(NAME), profile->GetChannel(i)->min, profile->GetChannel(i)->max, profile->GetChannel(i)->ToString(UNIT));
			}
		}

		if (recv & RECV_PROFILE)
		{
			log_msg("Received profile\n");
			log_msg("Device %08X\n", gateway.device->ID);
			eoProfile *profile = gateway.device->GetProfile();

			float f;
			uint8_t t;
			for (int i = 0; i<profile->GetChannelCount(); i++)
			{
				if (profile->GetValue( profile->GetChannel(i)->type, f) ==EO_OK)
					log_msg("%s %.2f %s\n", profile->GetChannel(i)->ToString(NAME),f,profile->GetChannel(i)->ToString(UNIT));
				if (profile->GetValue( profile->GetChannel(i)->type, t) ==EO_OK)
					log_msg("%s %u \n", profile->GetChannel(i)->ToString(NAME),t);
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
