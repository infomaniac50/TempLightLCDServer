/*
  Web Server
 
 A simple web server that shows the value of the analog input pins.
 using an Arduino Wiznet Ethernet shield. 
 
 Circuit:
 * Ethernet shield attached to pins 10, 11, 12, 13
 * Analog inputs attached to pins A0 through A5 (optional)
 
 created 18 Dec 2009
 by David A. Mellis
 modified 9 Apr 2012
 by Tom Igoe
 
 */
#include <Wire.h>
#include <SPI.h>
#include <Ethernet.h>
#include <SimpleTimer.h>
#include <EasyTransferI2C.h>
#include <Format.h>
#include <Flash.h>
#include <digitalWriteFast.h>

#define COMMAND_COUNT 5
#define ARGUMENT_COUNT 3

struct CMD_DATA
{
  boolean cmd;
  boolean fields[COMMAND_COUNT + ARGUMENT_COUNT];
  char cmds[COMMAND_COUNT];
  int args[ARGUMENT_COUNT];
};

struct VALUE_DATA
{
  int full;
  int ir;
  int vis;
  int lux;
  float temp_c;
  float temp_f;
  int r;
  int g;
  int b;
};

VALUE_DATA rxdata;
CMD_DATA txdata;

EasyTransferI2C ETtx;
EasyTransferI2C ETrx;

#define I2C_TX_ADDRESS 10
#define I2C_RX_ADDRESS 9

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = { 0x90, 0xA2, 0xDA, 0x00, 0x3F, 0x8A };
IPAddress ip(192, 168, 1, 3);
IPAddress dns_server(192, 168, 1, 2);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
// Initialize the Ethernet server library
// with the IP address and port you want to use 
// (port 80 is default for HTTP):
EthernetServer server(23);
EthernetClient client = 0;

SimpleTimer timer;

// Other global variables
#define textBuffSize 22 //length of longest command string plus two spaces for CR + LF
char textBuff[textBuffSize]; //someplace to put received text
int charsReceived = 0;
char * serialBuffer;
//we'll use a flag separate from client.connected
//so we can recognize when a new connection has been created
boolean connectFlag = 0; 

unsigned long timeOfLastActivity; //time in milliseconds of last activity
unsigned long allowedConnectTime = 300000; //five minutes

int passwordTimerId = -1;
int clientReadTimerId = -1;
int relayTimerId = -1;
#define STATUS_PIN 13
void setup() {
  Wire.begin(I2C_RX_ADDRESS);
  
  ETtx.begin(details(txdata), &Wire);
  ETrx.begin(details(rxdata), &Wire);
  Wire.onReceive(rxdata_receive);
  
  pinMode(STATUS_PIN, OUTPUT);
  digitalWrite(STATUS_PIN, LOW);
  txdata.cmd = false;
  
  for(int i = 0; i < COMMAND_COUNT + ARGUMENT_COUNT; i++)
  {
    txdata.fields[i] = false;
  }
  
  // Open serial communications and wait for port to open:
  Serial.begin(9600);
  // start the Ethernet connection and the server:
  Ethernet.begin(mac, ip, dns, gateway, subnet);
  server.begin();
  timer.setInterval(1000, clientConnect);
  timer.setInterval(5000, checkConnectionTimeout);
  passwordTimerId = timer.setInterval(1000, checkReceivedPassword);
  clientReadTimerId = timer.setInterval(1000, checkReceivedText);
  timer.disable(clientReadTimerId);
  timer.disable(passwordTimerId);  
}

void loop() {  
  timer.run();
  ETrx.receiveData(); 
}

void rxdata_receive(int numBytes) {
 
  static boolean value = false;
  
  value = !value;
  
  if (value)
    digitalWrite(STATUS_PIN, HIGH);
  else
    digitalWrite(STATUS_PIN, LOW);
}

void checkReceivedText()
{
  if (client.connected() && client.available()) getReceivedText();
}

void checkReceivedPassword()
{
  if (client.connected() && client.available()) getReceivedPassword();
}

void clientConnect()
{
  // look to see if a new connection is created,
  // print welcome message, set connected flag
  if (server.available() && !connectFlag) {
    connectFlag = 1;
    client = server.available();
    client.println("\r\nDerek's Arduino Telnet Server");    
//    client.println("? for help");
    printPasswordPrompt();
    
    timer.enable(passwordTimerId);
  }
}

//Need a password prompt here
//Figure out a way to do it with the getReceivedText function

void printPrompt()
{
  timeOfLastActivity = millis();
  client.flush();
  charsReceived = 0; //count of characters received
  client.print("\r\n>");
}

void printPasswordPrompt()
{
  client.print("\r\nEnter Password:");
  printPrompt();
}

void connectionStop()
{
  client.stop();
  connectFlag = 0;
  timer.disable(passwordTimerId);
  timer.disable(clientReadTimerId);
}

void checkConnectionTimeout()
{
  if(millis() - timeOfLastActivity > allowedConnectTime) {
    client.println();
    client.println("Timeout disconnect.");
    
    connectionStop();
  }
}

void getReceivedPassword()
{
  int result = readClient();
  
  //if CR found go look at received text and execute command
  if(result > 0) {
    parseReceivedPassword(); //Put a password checker in this function
    // after completing command, print a new prompt
    printPrompt();
  }

  // if textBuff full without reaching a CR, print an error message
  if(result == -2) {
    client.println();
    client.print("Buffer too large");
    printPrompt();
  }
  // if textBuff not full and no CR, do nothing else;
  // go back to loop until more characters are received

}

void getReceivedText()
{   
  int result = readClient();

  if(result > 0) {
    parseReceivedText(); //Put a password checker in this function
    
    printPrompt();
  }
  // if textBuff full without reaching a CR, print an error message
  if(result == -2) {
    client.println();
    printErrorMessage();
    printPrompt();
  }
  // if textBuff not full and no CR, do nothing else;
  // go back to loop until more characters are received
}

int readClient()
{
  char c;
  int charsWaiting;

  // copy waiting characters into textBuff
  //until textBuff full, CR received, or no more characters
  charsWaiting = client.available();
  do {
    c = client.read();
    textBuff[charsReceived] = c;
    charsReceived++;
    charsWaiting--;
  }
  while(charsReceived <= textBuffSize && c != 0x0d && charsWaiting > 0);

  //if CR found go look at received text and execute command
  if(c == 0x0d) {
    return charsReceived;
  }

  // if textBuff full without reaching a CR, print an error message
  if(charsReceived >= textBuffSize) {
    return -2;
  }
  // if textBuff not full and no CR, do nothing else;
  // go back to loop until more characters are received
  return -1;
}

void parseReceivedPassword()
{
  static int try_again = 0;
  boolean valid_password = true;
  
  for(int i = 0; i < 11; i++)
  {
    valid_password = valid_password && (password[i] == textBuff[i]);
  }
  
  if (valid_password)
  {
    timer.disable(passwordTimerId);
    timer.enable(clientReadTimerId);
    try_again = 0;
  }
  else
  {
    try_again++;
    if (try_again >= 3)
    {
      connectionStop();
      try_again = 0;
    }
  }  
}

void parseReceivedText()
{
  // look at first character and decide what to do
  switch (textBuff[0]) {
    case 't' : printTemp();              break;
    case 'l' : printLight();             break;
    case 'd' : printDebug();             break;
    case 'r' : sendMessage();            break;
    case 'c' : checkCloseConnection();   break;
    case '?' : printHelpMessage();	 break;
    case 0x0d :				 break;  //ignore a carriage return
    default: printErrorMessage();	 break;
  }
}

int peekNextDigit(int index)
{
    char c = textBuff[index];
  
    if (c == '-') return c;
    if (c >= '0' && c <= '9') return c;
    return -1;
}

long parseInt(int * index, char skipChar)
{
  boolean isNegative = false;
  long value = 0;
  int c;
  int i = 0;
  c = peekNextDigit(*index);
  // ignore non numeric leading characters
  if(c < 0)
    return 0; // zero returned if timeout

  do{
    if(c == '-')
      isNegative = true;
    else if(c >= '0' && c <= '9')        // is c a digit?
      value = value * 10 + c - '0';
    i++;  // consume the character we got with peek
    c = textBuff[*index + i];
  }
  while( (c >= '0' && c <= '9') || c == skipChar );
  
  if(isNegative)
    value = -value;
  return value;  
}

void sendMessage()
{
  txdata.cmd = true;
  
  for(int i = 0; i < COMMAND_COUNT + ARGUMENT_COUNT; i++)
    txdata.fields[i] = false;  
  
  int i = 2;
  char c;
  
  int cmd = 0;
  int arg = 0;
  
  while(i < charsReceived)
  {
    c = textBuff[i];
    
    if (c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z')
    {
      txdata.cmds[cmd] = c;
      txdata.fields[cmd] = true;
      cmd++;
    }
    else if(c >= '0' && c <= '9')
    {
      txdata.args[arg] = parseInt(&i, -1);
      txdata.fields[COMMAND_COUNT + arg] = true;
      arg++;
    }
    
    i++;
  }
  
  ETtx.sendData(I2C_TX_ADDRESS);  
  
}

void printTemp()
{
  String stemp_c;
  String stemp_f;
  int width;
  stemp_c = formatFloat(rxdata.temp_c, 1, &width);
  stemp_f = formatFloat(rxdata.temp_f, 1, &width);
  
  client.print("\r\n");
  client.print(stemp_c);
  client.print("C\r\n");
  client.print(stemp_f);
  client.print("F\r\n");   
}

void printLight()
{
  client.print("\r\n");
  client.print(rxdata.ir);
  client.print("\r\n");
  client.print(rxdata.full);
  client.print("\r\n");
  client.print(rxdata.vis);
  client.print("\r\n");
  client.print(rxdata.lux);
  client.print("\r\n");
}

void printDebug()
{
  client.print("\r\n");
  client.print("R = ");
  client.print(rxdata.r);
  client.print("\r\n");
  client.print(" G = ");
  client.print(rxdata.g);
  client.print("\r\n");
  client.print(" B = ");
  client.print(rxdata.b);
  client.print("\r\n");

}

void printErrorMessage()
{
  client.println("Unrecognized command.  ? for help.");
}

void checkCloseConnection()
  // if we got here, textBuff[0] = 'c', check the next two
  // characters to make sure the command is valid
{
  if (textBuff[1] == 'l' && textBuff[2] == 0x0d)
    closeConnection();
  else
    printErrorMessage();
}

void closeConnection()
{
  client.println("\r\nBye.\r\n");
  connectionStop();
}

void printHelpMessage()
{
  FLASH_STRING(sensorhelp, 
  "Welcome to Derek's Arduino Sensor Widget.\r\n"
  "Command Syntax: b|p|c|s <options>\r\n"
  "\r\n"
  "Configure Backlight\r\n"
  "\tb r|g|b|o {0..255}\r\n"
  "\t\tr: Red Level\r\n"
  "\t\tg: Green Level\r\n"
  "\t\tb: Blue Level\r\n"
  "\t\to: Overall Level\r\n"
  "Configure Sensor Display\r\n"
  "\tControls whether to display sensor values on the display.\r\n"
  "\tIf more than one sensor is on at a time the program will display\r\n"
  "\tthe current sensor, wait the update delay, then display the next sensor.\r\n"
  "\tp <option>\r\n"
  "\t\tt: Toggle Temperature Display\r\n"
  "\t\tl: Toggle Light Display\r\n"
  "Save and Load Configuration\r\n"
  "\tSaves the configuration in the EEPROM to persist over reboots.\r\n"
  "\tc r|w\r\n"
  "\t\tr: Read Config\r\n"
  "\t\tw: Write Config\r\n"
  "Configure Sensors\r\n"
  "\ts <sensor>\r\n"
  "\tConfigure Light Sensor\r\n"
  "\t\ts l i|g <option>\r\n"
  "\t\t\ti: Integration Time {1,2,3} -> {13ms,101ms,402ms}\r\n"
  "\t\t\tg: Gain {1,0} -> {16x,0x}\r\n"
  );

  client << sensorhelp;
}

void printHelp2()
{
  client.println("\r\nExamples of supported commands:\r\n");
  client.println("  dr	 -digital read:   returns state of digital pins 0 to 9");
  client.println("  dr4	-digital read:   returns state of pin 4 only");
  client.println("  ar	 -analog read:    returns all analog inputs");
  client.println("  dw0=hi   -digital write:  turn pin 0 on  valid pins are 0 to 9");
  client.println("  dw0=lo   -digital write:  turn pin 0 off valid pins are 0 to 9");
  client.println("  aw3=222  -analog write:   set digital pin 3 to PWM value 222");
  client.println("					allowed pins are 3,5,6,9");
  client.println("					allowed PWM range 0 to 255");
  client.println("  pm0=in   -pin mode:	 set pin 0 to INPUT  valid pins are 0 to 9");
  client.println("  pm0=ou   -pin mode:	 set pin 0 to OUTPUT valid pins are 0 to 9");
  client.println("  cl	 -close connection");
  client.println("  ?	  -print this help message");
}
