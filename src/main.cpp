/// @file tft_example_fancy_ripples.cpp
/// @brief Example code for a fancy FFT visualization with ripples


// Display
#include <Arduino_GFX_Library.h>
#define GFX_BL TFT_BL

Arduino_DataBus *bus = new Arduino_ESP32SPIDMA(TFT_DC /* DC */,
                                               TFT_CS /* CS */,
                                               TFT_SCLK /* SCK */,
                                               TFT_MOSI /* MOSI */,
                                               TFT_MISO /* MISO */,
                                               VSPI /* spi_num */,
                                               false /*shared interface*/);

// Arduino_DataBus *bus = new Arduino_ESP32SPI(
//   TFT_DC /* DC */,
//   TFT_CS /* CS */,
//   TFT_SCLK /* SCK */,
//   TFT_MOSI /* MOSI */,
//   TFT_MISO /* MISO */,
//   HSPI /* spi_num */
// );

Arduino_GFX *tft = new Arduino_ST7796(
    bus, TFT_RST /* RST */, 1 /* rotation */, true /* IPS */);
#define GFX_SPEED 54000000UL

#include <list>

// Display Settings (same as before)
#define DISPLAY_WIDTH 320
#define DISPLAY_HEIGHT 320
#define NUM_BANDS 8
#define NUM_ROWS 8

// LED Settings (from fancy example)
#define LED_DIAMETER 32
#define LED_SPACING 8
#define GLOW_SIZE 6
#define BLOCK_WIDTH (DISPLAY_WIDTH / NUM_BANDS)
#define BLOCK_HEIGHT (DISPLAY_HEIGHT / NUM_ROWS)

// Ripple Settings
#define MAX_RIPPLES 8        // Maximum number of concurrent ripples
#define RIPPLE_LIFETIME 2000 // How long a ripple lasts (ms)
#define RIPPLE_SPEED 0.15f   // How fast ripples expand
#define BASS_THRESHOLD 180   // Bass intensity to trigger ripples

// TFT_eSPI tft = TFT_eSPI();

// Structure to track individual ripples
struct Ripple
{
    float x, y;         // Ripple center
    float radius;       // Current radius
    uint32_t startTime; // When the ripple started
    uint8_t intensity;  // Initial intensity (affects color and opacity)
    float maxRadius;    // Maximum radius this ripple will reach
};

std::list<Ripple> activeRipples;

// Code from header of original code
// ---------------------------------
// #include "AudioFileSourcePROGMEM.h"
// #include "AudioGeneratorAAC.h"
// #include "AudioOutputI2S.h"
// #include "BluetoothA2DPSink.h"
// #include "icons.h"
// #include "sounds.h"

/* AudioTools */
#include <AudioTools.h>
#include <BluetoothA2DPSink.h>

#include <arduinoFFT.h>

// Audio Settings
// #define I2S_DOUT    A3 // A3 on QtPy
// #define I2S_BCLK    A1 // A1 QtPy
// #define I2S_LRC     A2 // A2 on QtPy
#define MODE_PIN -1

#define SAMPLES 512
#define SAMPLING_FREQUENCY 44100

#define BRIGHTNESS 50

#define DEVICE_NAME "bluelight2"



I2SStream i2s;
BluetoothA2DPSink a2dp_sink(i2s);

float amplitude = 200.0;

int32_t peak[] = {0, 0, 0, 0, 0, 0, 0, 0};
float vReal[SAMPLES];
float vImag[SAMPLES];

//arduinoFFT FFT = arduinoFFT();
ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, SAMPLES, SAMPLING_FREQUENCY);

double brightness = 0.15;

QueueHandle_t queueNotify;

int16_t sample_l_int;
int16_t sample_r_int;

int visualizationCounter = 0;
int32_t lastVisualizationUpdate = 0;

// static const i2s_pin_config_t pin_config = {.bck_io_num = I2S_BCLK,
//                                             .ws_io_num = I2S_LRC,
//                                             .data_out_num = I2S_DOUT,
//                                             .data_in_num = I2S_PIN_NO_CHANGE};

uint8_t hueOffset = 0;

// audio state management
bool devicePlayedAudio = false;
esp_a2d_audio_state_t currentAudioState = ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND;

// device connection management
bool bleDeviceConnected = false;
// ---------------------------------------

// Helper function to convert HSV to RGB with alpha
struct RGBA {
    uint8_t r, g, b, a;
};

RGBA hsvToRGBA(uint8_t h, uint8_t s, uint8_t v, uint8_t alpha) {
    RGBA color;
    float r, g, b;
    
    // Convert HSV to RGB (0-255 range)
    float hh = h / 255.0f * 360.0f;
    float ss = s / 255.0f;
    float vv = v / 255.0f;
    
    int i = (int)(hh / 60.0f);
    float ff = (hh / 60.0f) - i;
    float p = vv * (1.0f - ss);
    float q = vv * (1.0f - (ss * ff));
    float t = vv * (1.0f - (ss * (1.0f - ff)));

    switch(i) {
        case 0:  r = vv; g = t;  b = p;  break;
        case 1:  r = q;  g = vv; b = p;  break;
        case 2:  r = p;  g = vv; b = t;  break;
        case 3:  r = p;  g = q;  b = vv; break;
        case 4:  r = t;  g = p;  b = vv; break;
        default: r = vv; g = p;  b = q;  break;
    }
    
    color.r = (uint8_t)(r * 255);
    color.g = (uint8_t)(g * 255);
    color.b = (uint8_t)(b * 255);
    color.a = alpha;
    return color;
}

// Convert RGBA to RGB565 with alpha blending against black background
uint16_t rgbaToRGB565(RGBA color) {
    // Alpha blend with black background
    uint8_t r = (color.r * color.a) >> 8;
    uint8_t g = (color.g * color.a) >> 8;
    uint8_t b = (color.b * color.a) >> 8;
    
    // Convert to RGB565
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}


// Blend two RGB565 colors with alpha
uint16_t blendColors(uint16_t color1, uint16_t color2, uint8_t alpha)
{
    // Extract RGB components
    uint8_t r1 = (color1 >> 11) & 0x1F;
    uint8_t g1 = (color1 >> 5) & 0x3F;
    uint8_t b1 = color1 & 0x1F;

    uint8_t r2 = (color2 >> 11) & 0x1F;
    uint8_t g2 = (color2 >> 5) & 0x3F;
    uint8_t b2 = color2 & 0x1F;

    // Blend
    uint8_t r = ((r2 * alpha) + (r1 * (255 - alpha))) / 255;
    uint8_t g = ((g2 * alpha) + (g1 * (255 - alpha))) / 255;
    uint8_t b = ((b2 * alpha) + (b1 * (255 - alpha))) / 255;

    // Recombine
    return (r << 11) | (g << 5) | b;
}

// Create a new ripple at the specified position
void createRipple(float x, float y, uint8_t intensity)
{
    if (activeRipples.size() >= MAX_RIPPLES)
    {
        return; // Limit concurrent ripples
    }

    Ripple ripple;
    ripple.x = x;
    ripple.y = y;
    ripple.radius = LED_DIAMETER / 2; // Start at LED edge
    ripple.startTime = millis();
    ripple.intensity = intensity;
    // Randomize max radius a bit for variety
    ripple.maxRadius = DISPLAY_WIDTH * (0.7f + (random(30) / 100.0f));

    activeRipples.push_back(ripple);
}

// Update and draw all active ripples
void updateRipples()
{
    uint32_t currentTime = millis();

    // Update ripple positions and remove dead ones
    for (auto it = activeRipples.begin(); it != activeRipples.end();)
    {
        uint32_t age = currentTime - it->startTime;

        if (age >= RIPPLE_LIFETIME || it->radius >= it->maxRadius)
        {
            it = activeRipples.erase(it);
            continue;
        }

        // Update radius
        it->radius += RIPPLE_SPEED * (it->maxRadius / DISPLAY_WIDTH) *
                      (1.0f - (float)age / RIPPLE_LIFETIME) * 60; // Slow down as it ages

        // Calculate opacity based on age and initial intensity
        float lifeProgress = (float)age / RIPPLE_LIFETIME;
        uint8_t alpha = (uint8_t)(it->intensity * (1.0f - lifeProgress) * 0.5f);

        // Draw ripple circle with gradient
        uint16_t rippleColor = tft->color565(
            it->intensity * (1.0f - lifeProgress),        // R
            it->intensity * (1.0f - lifeProgress) * 0.5f, // G
            it->intensity                                 // B
        );

        // Draw as a series of concentric circles for gradient effect
        float thickness = 4.0f;
        for (float r = it->radius - thickness; r <= it->radius; r += 1.0f)
        {
            if (r < 0)
                continue;
            // Fade alpha based on distance from ripple edge
            uint8_t ringAlpha = alpha * (1.0f - (it->radius - r) / thickness);
            
            //tft->drawCircle(it->x, it->y, r, rippleColor); // original
            
            // use writeEllipseHelper inside of startWrite and endWrite (called elsewhere)
            tft->writeEllipseHelper(it->x, it->y, r, r, 0xFF, rippleColor); // cornername 0xF (or 0xFF) draws all 4 quarters
        }

        ++it;
    }
}

// Enhanced version of drawLED that can trigger ripples
void drawLED(uint8_t band, uint8_t row, uint8_t hue, uint8_t sat, uint8_t val, bool checkForRipple = true)
{
    // Calculate center position (same as before)
    int centerX = (band * BLOCK_WIDTH) + (BLOCK_WIDTH / 2);
    int centerY = (row * BLOCK_HEIGHT) + (BLOCK_HEIGHT / 2);

    // Check if this LED should trigger a ripple
    if (checkForRipple && band < 2 && val > BASS_THRESHOLD)
    { // Bass frequencies
        createRipple(centerX, centerY, val);
    }

    // Draw LED (same as in tft_example_fancy.cpp)
    if (val > 0) {  // Only draw if LED is on
    //  Serial.println(" Drawing LED");

        // Draw glow (larger, semi-transparent circle)
        // int glowRadius = (LED_DIAMETER / 2) + GLOW_SIZE;
        // RGBA glowColor = hsvToRGBA(hue, sat, val, 128); // 50% transparent
        // uint16_t glowRGB = rgbaToRGB565(glowColor);
        // tft->fillCircle(centerX, centerY, glowRadius, glowRGB);
        
        // Draw main LED (smaller, solid circle)
        RGBA ledColor = hsvToRGBA(hue, sat, val, 255); // Fully opaque
        uint16_t ledRGB = rgbaToRGB565(ledColor);

        // use write method inside of startWrite and endWrite (called elsewhere)
        tft->writeFillRect(centerX - (LED_DIAMETER/2), centerY - (LED_DIAMETER/2), LED_DIAMETER, LED_DIAMETER, ledRGB);
        // other ways to draw this:
        //tft->fillRoundRect(centerX - (LED_DIAMETER/2), centerY - (LED_DIAMETER/2), LED_DIAMETER, LED_DIAMETER, 4, ledRGB);
        //tft->fillCircle(centerX, centerY, LED_DIAMETER/2, ledRGB);
        
        // Add highlight (small white dot for 3D effect)
        // int highlightX = centerX - (LED_DIAMETER/6);
        // int highlightY = centerY - (LED_DIAMETER/6);
        // tft->fillCircle(highlightX, highlightY, 2, WHITE);
    } else {
        //Serial.println(" Not drawing LED"); 
        // Draw dark circle for off state

        // use write method inside of startWrite and endWrite (called elsewhere)
        tft->writeFillRect(centerX - (LED_DIAMETER/2), centerY - (LED_DIAMETER/2), LED_DIAMETER, LED_DIAMETER, BLACK);
        
        // other ways to draw this:
        //tft->fillRect(centerX - (LED_DIAMETER/2), centerY - (LED_DIAMETER/2), LED_DIAMETER, LED_DIAMETER, BLACK);
        //tft->fillRoundRect(centerX - (LED_DIAMETER/2), centerY - (LED_DIAMETER/2), LED_DIAMETER, LED_DIAMETER, 4, DARKGREY);
        //tft->fillCircle(centerX, centerY, LED_DIAMETER/2, DARKGREY);
    }
}

void createBands(int i, int dsize)
{
    uint8_t band = 0;
    if (i <= 2)
    {
        band = 0; // 125Hz
    }
    else if (i <= 5)
    {
        band = 1; // 250Hz
    }
    else if (i <= 7)
    {
        band = 2; // 500Hz
    }
    else if (i <= 15)
    {
        band = 3; // 1000Hz
    }
    else if (i <= 30)
    {
        band = 4; // 2000Hz
    }
    else if (i <= 53)
    {
        band = 5; // 4000Hz
    }
    else if (i <= 106)
    {
        band = 6; // 8000Hz
    }
    else
    {
        band = 7;
    }
    int dmax = amplitude;
    if (dsize > dmax)
        dsize = dmax;
    if (dsize > peak[band])
    {
        peak[band] = dsize;
    }
}

void renderFFT(void *parameter)
{

    int item = 0;
    for (;;)
    {
        if (uxQueueMessagesWaiting(queueNotify) > 0)
        {

             FFT.windowing(FFTWindow::Hamming, FFTDirection::Forward);	/* Weigh data */ 
             //FFT.Windowing(vReal, SAMPLES, FFT_WIN_TYP_HAMMING, FFT_FORWARD);

            FFT.compute(FFTDirection::Forward); /* Compute FFT */ 
            //FFT.Compute(vReal, vImag, SAMPLES, FFT_FORWARD);
            
             FFT.complexToMagnitude(); /* Compute magnitudes */
            //FFT.ComplexToMagnitude(vReal, vImag, SAMPLES);

            for (uint8_t band = 0; band < NUM_BANDS; band++)
            {
                peak[band] = 0;
            }

            // Don't use sample 0 and only first SAMPLES/2 are usable. Each array elelement represents a frequency and its value the amplitude.
            for (int i = 2; i < (SAMPLES / 2); i++)
            {
                if (vReal[i] > 1000)
                { // Add a crude noise filter, 10 x amplitude or more
                    createBands(i, (int)vReal[i] / amplitude);
                }
            }

            // Release handle
            xQueueReceive(queueNotify, &item, 0);

            uint8_t intensity;

            // begin of new code
            static uint32_t lastFrame = 0;
            const uint32_t frameTime = 1000 / 30; // Target 30fps

            tft->startWrite();

            while (true)
            {
                uint32_t currentTime = millis();
                if (currentTime - lastFrame < frameTime)
                {
                    delay(1);
                    continue;
                }
                lastFrame = currentTime;

                // Create a black background
                tft->fillScreen(BLACK);

                // Update and draw ripple effects first
                updateRipples();

                // Draw LEDs on top of ripples
                for (byte band = 0; band < NUM_BANDS; band++)
                {
                    intensity = map(peak[band], 1, amplitude, 0, NUM_ROWS);

                    for (int i = 0; i < NUM_ROWS; i++)
                    {
                        if (i >= intensity)
                        {
                            drawLED(band, NUM_ROWS - 1 - i, 0, 0, 0, false);
                        }
                        else
                        {
                            drawLED(band, NUM_ROWS - 1 - i, i * 16, 255, 255, true);
                        }
                    }
                }
            }

            tft->endWrite();

            // end of new code
            if ((millis() - lastVisualizationUpdate) > 1000)
            {
                log_e("Fps: %f", visualizationCounter / ((millis() - lastVisualizationUpdate) / 1000.0));
                visualizationCounter = 0;
                lastVisualizationUpdate = millis();
                hueOffset += 5;
            }
            visualizationCounter++;
        }
    }
}

void audio_data_callback(const uint8_t *data, uint32_t len)
{
    int item = 0;
    // Only prepare new samples if the queueNotify is empty
    if (uxQueueMessagesWaiting(queueNotify) == 0)
    {
        // log_e("Queue is empty, adding new item");
        int byteOffset = 0;
        for (int i = 0; i < SAMPLES; i++)
        {
            sample_l_int = (int16_t)(((*(data + byteOffset + 1) << 8) | *(data + byteOffset)));
            sample_r_int = (int16_t)(((*(data + byteOffset + 3) << 8) | *(data + byteOffset + 2)));
            vReal[i] = (sample_l_int + sample_r_int) / 2.0f;
            vImag[i] = 0;
            byteOffset = byteOffset + 4;
        }

        // Tell the task in core 1 that the processing can start
        xQueueSend(queueNotify, &item, portMAX_DELAY);
    }
}

void connection_state_changed(esp_a2d_connection_state_t state, void *)
{
    log_i("Connection state changed, new state: %d", state);
    if (ESP_A2D_CONNECTION_STATE_CONNECTED == state)
    {
        bleDeviceConnected = true;
        Serial.println("Connected");
    }
    else
    {
        bleDeviceConnected = false;
        Serial.println("Disconnected");
    }
}

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("Starting TFT");

// Initialize TFT
#ifdef GFX_EXTRA_PRE_INIT
    GFX_EXTRA_PRE_INIT();
#endif

    if (!tft->begin(GFX_SPEED))
    {
        Serial.println("tft->begin() failed!");
    }
    tft->fillScreen(BLACK);
    tft->setTextColor(WHITE, BLACK);
    tft->setCursor(10, 10);
    tft->setTextSize(2);
    tft->println("FFT Fancy Ripples");

    Serial.println("TFT initialized");

#ifdef GFX_BL
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR < 3)
    if (GFX_BL >= 1)
    {
        ledcSetup(0, 1000, 8);
        ledcAttachPin(GFX_BL, 0);
        ledcWrite(0, 204);
    }
#else  // ESP_ARDUINO_VERSION_MAJOR >= 3
    if (GFX_BL >= 1)
    {
        ledcAttachChannel(GFX_BL, 1000, 8, 1);
        ledcWrite(GFX_BL, 204);
    }
    else
    {
        Serial.println("GFX_BL not set to valid pin");
    }
#endif // ESP_ARDUINO_VERSION_MAJOR >= 3
#endif // GFX_BL

    // ... (rest of your setup code) ...

    // pinMode(MODE_PIN, OUTPUT);
    // pinMode(pushButton, INPUT);
    // digitalWrite(MODE_PIN, HIGH); // original
    // digitalWrite(MODE_PIN, LOW); // use for MUTE pin on 1334 dac, set low to unmute, high to mute

    // FastLED.addLeds<WS2812B, DATA_PIN>(leds, LED_COUNT);

    // playBootupSound();

    // The queueNotify is used for communication between A2DP callback and the FFT
    // processor
    Serial.println("Creating A2DP->FFT queueNotify");
    queueNotify = xQueueCreate(1, sizeof(int));
    if (queueNotify == NULL)
    {
        log_i("Error creating the A2DP->FFT queueNotify");
    }
    else
    {
        Serial.println("A2DP->FFT queueNotify created");
    }

    // This task will process the data acquired by the Bluetooth audio stream
    xTaskCreatePinnedToCore(renderFFT,      // Function that should be called
                            "FFT Renderer", // Name of the task (for debugging)
                            10000,          // Stack size (bytes)
                            NULL,           // Parameter to pass
                            1,              // Task priority
                            NULL,           // Task handle
                            1               // Core you want to run the task on (0 or 1)
    );

    // a2dp_sink.set_pin_config(pin_config);

    // a2dp_sink.start((char *)DEVICE_NAME);

    Serial.println("Starting Bluetooth Audio Sink");

    /* Bluetooth Audio sink */
    auto cfg = i2s.defaultConfig();
    cfg.pin_bck = I2S_BCLK;
    cfg.pin_ws = I2S_LRC;
    cfg.pin_data = I2S_DOUT;
    i2s.begin(cfg);

    // redirecting audio data to do FFT
    a2dp_sink.set_stream_reader(audio_data_callback, true);
    a2dp_sink.set_on_connection_state_changed(connection_state_changed);

    a2dp_sink.start((char *)DEVICE_NAME, true);
}

void loop()
{
    // For some reason the audio state changed callback doesn't work properly -> need to fetch the state here.
    //
    // Otherwise you could hook this up in setup() as sketched below.
    //
    // void audio_state_changed(esp_a2d_audio_state_t state, void *){
    //   log_i("audio state: %d", state);
    // }
    // a2dp_sink.set_on_audio_state_changed(audio_state_changed);

    esp_a2d_audio_state_t state = a2dp_sink.get_audio_state();
    if (currentAudioState != state)
    {
        log_i("Audio state changed; new state: %d", state);
        Serial.println("Audio state changed");
        currentAudioState = state;

        switch (state)
        {
        // Unclear how stopped and remote suspend really differ from one another. In
        // ESP32-A2DP >= v1.6 we seem to be getting the later when the client stops
        // audio playback.
        case ESP_A2D_AUDIO_STATE_REMOTE_SUSPEND:
        case ESP_A2D_AUDIO_STATE_STOPPED:
            Serial.println("Audio stopped");
            // if (bleDeviceConnected) {
            //   if (devicePlayedAudio) {
            //     //drawIcon(PAUSE);
            //   } else {
            //     //drawIcon(BLE);
            //   }
            // } else {
            //   //drawIcon(HEART);
            // }
            break;
        case ESP_A2D_AUDIO_STATE_STARTED:
            Serial.println("Audio started");
            devicePlayedAudio = true;
            break;
        }
    }
}