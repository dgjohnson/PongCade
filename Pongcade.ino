#include "Pongcade.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FastLED.h>
#include "DFRobotDFPlayerMini.h"
#include <FiniteStateMachine.h>
#include <LEDMatrix.h>
#include <LEDSprites.h>
#include <LEDText.h>
#include <FontMatrise.h>
#include <SimpleKalmanFilter.h>

// Change the next 6 defines to match your matrix type and size
#define LED_PIN        5
#define COLOR_ORDER    GRB
#define CHIPSET        WS2812B
#define MATRIX_WIDTH   24
#define MATRIX_HEIGHT  8
#define MATRIX_TYPE    VERTICAL_MATRIX

const char* ssid = STASSID;
const char* password = STAPSK;

//slider variables
const int sliderPin = A0;  // ESP8266 Analog Pin ADC0 = A0
int slider[] = {0,0};    //slider position in pixel offset (0-5)
SimpleKalmanFilter kalmanFilter[] = {SimpleKalmanFilter(2, 2, 0.01),SimpleKalmanFilter(2, 2, 0.01)};

/** definitions of the states */
State gameAttract = State(gameAttractEnter, gameAttractUpdate, NULL);  //attract mode
State gamePlay = State(gamePlayEnter, gamePlayUpdate, gamePlayExit);  //ball in play
State playerScore = State(playerScoreEnter, playerScoreUpdate, NULL); //display player Scores

/** the state machine controls which of the states get attention and execution time */
FSM stateMachine = FSM(gameAttract); //initialize state machine, start in state: gameAttract

int playerScores[] = {0,0};
uint16_t lastBallXChange=1;
bool gamePlayFirstLoop=true;

cLEDText screenMsg;
const unsigned char TxtAttract[] = {EFFECT_FRAME_RATE "\x05" EFFECT_BACKGND_LEAVE EFFECT_SCROLL_UP "    PONG    " EFFECT_DELAY_FRAMES "\x01\x2c"};
unsigned char TxtScore[25];

cLEDMatrix<MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> leds;
DFRobotDFPlayerMini myDFPlayer;

#define TARGET_FRAME_TIME    40  // Desired update rate, though if too many leds it will just run as fast as it can!
uint16_t PlasmaTime, PlasmaShift;
uint32_t LoopDelayMS, LastLoop, firstLoopMS;

const uint8_t BallData[] = 
{
  B8_1BIT(10000000)
};
const uint8_t PaddleData[] = 
{
  B8_1BIT(10000000),
  B8_1BIT(10000000),
  B8_1BIT(10000000)
};
struct CRGB ColTableOne[1] = { CRGB(255, 255, 255) };
struct CRGB ColTableTwo[1] = { CRGB(255, 255, 0) };

cLEDSprites Sprites(&leds);
cSprite SprBall(1, 1, BallData, 1, _1BIT, ColTableTwo, BallData);
cSprite SprPaddleOne(1, 3, PaddleData, 1, _1BIT, ColTableOne, PaddleData);
cSprite SprPaddleTwo(1, 3, PaddleData, 1, _1BIT, ColTableOne, PaddleData);

bool OTA_MODE = false;

void setup()
{
  LoopDelayMS = TARGET_FRAME_TIME;
  LastLoop = millis() - LoopDelayMS;
  
  //setup pins
  pinMode(14,OUTPUT);//piezo
  pinMode(5,OUTPUT);//LEDs
  
  pinMode(12,OUTPUT);//A
  pinMode(13,OUTPUT);//B
  updateSliderValues();

  pinMode(4,INPUT);//start
  pinMode(15,INPUT);//select

  //setup DFPlayer
  Serial.begin(9600);
  myDFPlayer.begin(Serial);
  myDFPlayer.volume(20);  //Set volume value. From 0 to 30

  if(digitalRead(4)==HIGH)
  {
    OTA_MODE=true;//set flag for loop

    //Setup OTA Mode
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      delay(5000);
      ESP.restart();
    }

    // Port defaults to 8266
    // ArduinoOTA.setPort(8266);
  
    // Hostname defaults to esp8266-[ChipID]
    ArduinoOTA.setHostname("PONG-OTA");
  
    // No authentication by default
    // ArduinoOTA.setPassword("admin");
  
    // Password can be set with it's md5 value as well
    // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
    // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");
  
    ArduinoOTA.onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH) {
        type = "sketch";
      } else { // U_FS
        type = "filesystem";
      }
  
      // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    });
    ArduinoOTA.onEnd([]() {
      myDFPlayer.play(8);  //Play the finished prompt
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      //float Progress=(progress / (total / 100));
      //myDFPlayer.play(9);  //Play a progress beep
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {
        //do some fast led stuff?
      } else if (error == OTA_BEGIN_ERROR) {
        //do some fast led stuff?
      } else if (error == OTA_CONNECT_ERROR) {
        //do some fast led stuff?
      } else if (error == OTA_RECEIVE_ERROR) {
        //do some fast led stuff?
      } else if (error == OTA_END_ERROR) {
        //do some fast led stuff?
      }
      myDFPlayer.play(10);  //Play an error sound
    });
  
    // enable if should be in OTA mode:
    ArduinoOTA.begin();
    myDFPlayer.play(7);  //Play the online prompt

  }  

  //game variables
  PlasmaShift = (random8(0, 5) * 32) + 64;
  PlasmaTime = 0;

  //Setup FastLED
  FastLED.addLeds<CHIPSET, LED_PIN, COLOR_ORDER>(leds[0], leds.Size());
  FastLED.setBrightness(100);
  FastLED.clear(true);
  FastLED.show();
}

void loop()
{
  if(OTA_MODE)
  {
      ArduinoOTA.handle();
  }
  else
  {
      stateMachine.update();
  }
}

void updateSliderValues()
{
    // read slider 0
    digitalWrite(12,LOW);//B
    digitalWrite(13,LOW);//A
    delay(2);
    slider[0] = round(kalmanFilter[0].updateEstimate(analogRead(sliderPin))/204);
    
    // read slider 1
    digitalWrite(12,LOW);//B
    digitalWrite(13,HIGH);//A
    delay(2);
    slider[1] = round(kalmanFilter[1].updateEstimate(analogRead(sliderPin))/204);

}

void drawPlasma()
{
    // Fill background with dim plasma
    #define PLASMA_X_FACTOR  24
    #define PLASMA_Y_FACTOR  24
    for (int16_t x=0; x<MATRIX_WIDTH; x++)
    {
      for (int16_t y=0; y<MATRIX_HEIGHT; y++)
      {
        int16_t r = sin16(PlasmaTime) / 256;
        int16_t h = sin16(x * r * PLASMA_X_FACTOR + PlasmaTime) + cos16(y * (-r) * PLASMA_Y_FACTOR + PlasmaTime) + sin16(y * x * (cos16(-PlasmaTime) / 256) / 2);
        leds(x, y) = CHSV((uint8_t)((h / 256) + 128), 255, 75);
      }
    }
    uint16_t OldPlasmaTime = PlasmaTime;
    PlasmaTime += PlasmaShift;
    if (OldPlasmaTime > PlasmaTime)
      PlasmaShift = (random8(0, 5) * 32) + 64;

}
//waiting to play a game
void gameAttractEnter()
{
  //scrolling text
  screenMsg.SetFont(MatriseFontData);
  screenMsg.Init(&leds, leds.Width(), screenMsg.FontHeight() + 1, 0, 0);
  screenMsg.SetText((unsigned char *)TxtAttract, sizeof(TxtAttract) - 1);
  screenMsg.SetTextColrOptions(COLR_RGB | COLR_VERT | COLR_GRAD, 0x7f, 0x00, 0x7f, 0x00, 0x00, 0x7f);
  myDFPlayer.play(1);  //Play the intro mp3
  LoopDelayMS = TARGET_FRAME_TIME;

}
void gameAttractUpdate()
{
  static int color=1;
  if (abs((long)(millis() - LastLoop)) >= TARGET_FRAME_TIME)
  {
    LastLoop = millis();
    FastLED.clear();
    //draw background
    drawPlasma();

    color+=5;
    screenMsg.SetTextColrOptions(COLR_HSV | COLR_VERT | COLR_GRAD, color%255, 255, 255, (color+128)%255, 255, 255);
    //draw text
    if (screenMsg.UpdateText() == -1)
    {
      screenMsg.SetText((unsigned char *)TxtAttract, sizeof(TxtAttract) - 1);
    }
    FastLED.show();
  }
  if(digitalRead(15)==HIGH)
  {
      myDFPlayer.play(1);  //Play button sound mp3
      stateMachine.transitionTo(gamePlay); 
  }
}

// begin gameplay
void gamePlayEnter()
{
  updateSliderValues();
  SprBall.SetPositionFrameMotionOptions((MATRIX_WIDTH)/2/*X*/, MATRIX_HEIGHT/2/*Y*/, 0/*Frame*/, 0/*FrameRate*/, lastBallXChange/*XChange*/, 1/*XRate*/, +1/*YChange*/, 3/*YRate*/, SPRITE_DETECT_EDGE | SPRITE_DETECT_COLLISION | SPRITE_Y_KEEPIN );
  SprPaddleOne.SetPositionFrameMotionOptions(0/*X*/, slider[0]/*Y*/, 0/*Frame*/, 0/*FrameRate*/, 0/*XChange*/, 0/*XRate*/, 0/*YChange*/, 0/*YRate*/);
  SprPaddleTwo.SetPositionFrameMotionOptions(23/*X*/, slider[1]/*Y*/, 0/*Frame*/, 0/*FrameRate*/, 0/*XChange*/, 0/*XRate*/, 0/*YChange*/, 0/*YRate*/);

  Sprites.AddSprite(&SprBall);
  Sprites.AddSprite(&SprPaddleOne);
  Sprites.AddSprite(&SprPaddleTwo);

  gamePlayFirstLoop=true;
  firstLoopMS=millis();
}
void gamePlayUpdate()
{
  if (abs((long)(millis() - LastLoop)) >= LoopDelayMS)
  {
    LastLoop = millis();
    FastLED.clear();
    
    //update sprites
    Sprites.UpdateSprites();
    Sprites.DetectCollisions();
    if (SprBall.GetFlags() & SPRITE_COLLISION)
    {
      if (SprPaddleOne.GetFlags() & SPRITE_COLLISION || SprPaddleTwo.GetFlags() & SPRITE_COLLISION )
      {
        //reverse direction
        lastBallXChange=-SprBall.GetXChange();
        SprBall.SetXChange(lastBallXChange);
        
        //increase speed
        LoopDelayMS=(millis()%2==0)?max((int)(LoopDelayMS-1),10):LoopDelayMS;//only reduce the speed if millis()%2==0
        
        //change ball angle
        int deltaY=((SprPaddleOne.GetFlags() & SPRITE_COLLISION)?SprPaddleOne.m_Y:SprPaddleTwo.m_Y)- SprBall.m_Y;
        SprBall.SetYChange(deltaY<0?+1:-1);
        SprBall.SetYCounter(abs(deltaY)+random(6));
        
        //play ponk sound
        myDFPlayer.play(2);  //Play reflect mp3

      }
    }
    else if(SprBall.GetFlags() & SPRITE_EDGE_X_MIN)
    {
      //score player two
      playerScores[1]++;

      //play sound
      myDFPlayer.play(4);  //Play miss sound        
      
      //display score
      stateMachine.transitionTo(playerScore);

    }
    else if(SprBall.GetFlags() & SPRITE_EDGE_X_MAX)
    {
      //score player one
      playerScores[0]++;
      
      //play sound
      myDFPlayer.play(4);  //Play miss sound        
      
      //display score
      stateMachine.transitionTo(playerScore); 
    }
    else if((SprBall.m_X>6&&SprBall.m_X<MATRIX_WIDTH-6)&&((SprBall.GetFlags() & SPRITE_EDGE_Y_MIN)||(SprBall.GetFlags() & SPRITE_EDGE_Y_MAX)))
    {
      //play sound
      myDFPlayer.play(3);  //Play wall sound        
    }

    
    do//pause if returning to gameplay mode, otherwise run this once
    {
      //update Slider Positions
      updateSliderValues();
      SprPaddleOne.m_Y=slider[1];
      SprPaddleTwo.m_Y=slider[0];

      //update screen
      drawPlasma();
      Sprites.RenderSprites();
      FastLED.show();
      delay(gamePlayFirstLoop?LoopDelayMS:0);//generate plasma at framerate.
    }while(gamePlayFirstLoop && millis()-firstLoopMS<750);
    
    gamePlayFirstLoop=false;
  }
}

void gamePlayExit()
{
  lastBallXChange= SprBall.GetXChange();
  Sprites.RemoveSprite(&SprBall);
  Sprites.RemoveSprite(&SprPaddleOne);
  Sprites.RemoveSprite(&SprPaddleTwo);
}

// display player scores
void playerScoreEnter()
{
  screenMsg.SetFont(MatriseFontData);
  screenMsg.Init(&leds, leds.Width(), screenMsg.FontHeight() + 1, 0, 0);

  char SpaceMsg[2];
  sprintf((char *)SpaceMsg, "%.*s", (playerScores[0]>9||playerScores[1]>9)?1:2, "  ");

  //color the winning score green and the losing orange
  if(playerScores[1]>playerScores[0])
  {
    sprintf((char *)TxtScore,EFFECT_FRAME_RATE "\x03" EFFECT_BACKGND_LEAVE EFFECT_SCROLL_UP "    " EFFECT_HSV "\x0a\xff\xff%u" EFFECT_HSV "\x60\xff\xff" "%s%u", playerScores[0], SpaceMsg, playerScores[1]);
  } else {
    sprintf((char *)TxtScore,EFFECT_FRAME_RATE "\x03" EFFECT_BACKGND_LEAVE EFFECT_SCROLL_UP "    " EFFECT_HSV "\x60\xff\xff%u" EFFECT_HSV "\x0a\xff\xff" "%s%u", playerScores[0], SpaceMsg, playerScores[1]);
  }
  screenMsg.SetText(TxtScore, strlen((const char *)TxtScore));
  screenMsg.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0x00, 0xff);

  //play win sound
  if(playerScores[0]>9)
  {
      myDFPlayer.play(5);  //Player one wins sound        
  }
  else if(playerScores[1]>9)
  {
      myDFPlayer.play(6);  //Player two wins sound        
  }

}
void playerScoreUpdate()
{
  if (abs((long)(millis() - LastLoop)) >= TARGET_FRAME_TIME)
  {
    LastLoop = millis();
    FastLED.clear();
    //draw background
    drawPlasma();
    
  
    //draw text
    if (screenMsg.UpdateText() == -1)
    {
      if(playerScores[0]>9||playerScores[1]>9)
      {
        playerScores[0]=playerScores[1]=0;
        //reset game speed
        LoopDelayMS = TARGET_FRAME_TIME;
        stateMachine.transitionTo(gameAttract);
      }
      else
      {
        stateMachine.transitionTo(gamePlay);
      }
    }
    FastLED.show();
  }
}
