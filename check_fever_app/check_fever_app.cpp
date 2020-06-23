#include <stdlib.h>
#include <iostream>
#include <signal.h>
#include <string>
#include <chrono>
#include <thread>

#include "Lepton3.hpp"

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "stopwatch.hpp"

using namespace std;

// ----> Global variables
Lepton3* lepton3=nullptr;
static bool close = false;

cv::Mat frame16;
cv::Mat frameRGB;

double img_scale_fact = 3.0;

uint16_t min_raw16;
uint16_t max_raw16;

std::string temp_str;
std::string win_name = "Temperature stream";

// Hypothesis: sensor is linear.
// If the range of the sensor is [-10,140] °C in High Gain mode, we can calculate the threasholds
// for "life temperature" between 30.0°C and 37.0°C
double scale_factor = 0.0092; // 150/(2^14-1))
// <---- Global variables

// ----> Global functions
void close_handler(int s);
void keyboard_handler(int key);

void set_rgb_mode(bool enable);
cv::Mat normalizeFrame( const cv::Mat& frame16, uint16_t min, uint16_t max );

void mouseCallBackFunc(int event, int x, int y, int flags, void* userdata);
// <---- Global functions

int main (int argc, char *argv[])
{
    cout << "Check Fever App for Lepton3 on Nvidia Jetson" << endl;

    // ----> Set Ctrl+C handler
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = close_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);
    // <---- Set Ctrl+C handler

    Lepton3::DebugLvl deb_lvl = Lepton3::DBG_NONE;

    lepton3 = new Lepton3( "/dev/spidev0.0", "/dev/i2c-0", deb_lvl ); // use SPI1 and I2C-1 ports
    lepton3->start();

    // Set initial data mode
    set_rgb_mode(false); // 16 bit raw data required

    if( lepton3->setGainMode( LEP_SYS_GAIN_MODE_HIGH ) == LEP_OK )
    {
        LEP_SYS_GAIN_MODE_E gainMode;
        if( lepton3->getGainMode( gainMode ) == LEP_OK )
        {
            string str = (gainMode==LEP_SYS_GAIN_MODE_HIGH)?string("High"):((gainMode==LEP_SYS_GAIN_MODE_LOW)?string("Low"):string("Auto"));
            cout << " * Gain mode: " << str << endl;
        }
    }

    uint64_t frameIdx=0;

    uint8_t w,h;

    // ----> People detection thresholds
    double min_norm_temp = 30.0f;
    double warn_temp = 37.0f;
    double fever_temp = 37.5f;
    double max_temp = 42.0f;

    uint16_t min_norm_raw = static_cast<uint16_t>(min_norm_temp/scale_factor);
    uint16_t warn_raw = static_cast<uint16_t>(warn_temp/scale_factor);
    uint16_t fever_raw = static_cast<uint16_t>(fever_temp/scale_factor);
    uint16_t max_raw = static_cast<uint16_t>(max_temp/scale_factor);

    if( lepton3->enableRadiometry( true ) != LEP_OK)
    {
        cerr << "Failed to enable radiometry!" << endl;
        return EXIT_FAILURE;
    }
    // <---- People detection thresholds

    // ----> Set OpenCV output window and mouse callback
    //Create a window
    cv::namedWindow(win_name, cv::WINDOW_AUTOSIZE);

    //set the callback function for any mouse event
    cv::setMouseCallback(win_name, mouseCallBackFunc, NULL);
    // <---- Set OpenCV output window and mouse callback

    StopWatch stpWtc;

    stpWtc.tic();

    bool initialized = false;

    while(!close)
    {
        const uint16_t* data16 = lepton3->getLastFrame16( w, h, &min_raw16, &max_raw16 );

        if(!initialized)
        {
            frame16 = cv::Mat( h, w, CV_16UC1 );
            frameRGB = cv::Mat( h, w, CV_8UC3 );

            initialized = true;
        }

        cv::Mat dispFrame;

        if( data16 )
        {
            double period_usec = stpWtc.toc();
            stpWtc.tic();

            double freq = (1000.*1000.)/period_usec;

            memcpy( frame16.data, data16, w*h*sizeof(uint16_t) );

            //cout << " * Central value: " << (int)(frame16.at<uint16_t>(w/2 + h/2*w )) << endl;

            // ----> Rescaling/Normalization to 8bit
            cv::Mat rescaled;
            frame16.copyTo(rescaled);
            double diff = static_cast<double>(max_raw16 - min_raw16); // Image range
            double scale = 255./diff; // Scale factor

            rescaled -= min_raw16; // Bias
            rescaled *= scale; // Rescale data

            rescaled.convertTo( dispFrame, CV_8UC3 );
            // <---- Rescaling/Normalization to 8bit


            cv::Mat rescaledImg;
            cv::resize( dispFrame, rescaledImg, cv::Size(), img_scale_fact, img_scale_fact);
            cv::imshow( win_name, rescaledImg );
            int key = cv::waitKey(5);
            if( key == 'q' || key == 'Q')
            {
                close=true;
            }

            keyboard_handler(key);

            frameIdx++;

            if( deb_lvl>=Lepton3::DBG_INFO  )
            {
                cout << "> Frame period: " << period_usec <<  " usec - FPS: " << freq << endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    delete lepton3;

    return EXIT_SUCCESS;
}

void close_handler(int s)
{
    if(s==2)
    {
        cout << endl << "Ctrl+C pressed..." << endl;
        close = true;
    }
}

void keyboard_handler(int key)
{
    switch(key)
    {
    case 'f':
        if( lepton3->doFFC() == LEP_OK )
        {
            cout << " * FFC completed" << endl;
        }
        break;

    case 'F':
        if( lepton3->doRadFFC() == LEP_OK )
        {
            cout << " * Radiometry FFC completed" << endl;
        }
        break;

    default:
        break;
    }
}

void set_rgb_mode(bool enable)
{
    bool rgb_mode = enable;

    if( lepton3->enableRadiometry( !rgb_mode ) < 0)
    {
        cerr << "Failed to set radiometry status" << endl;
    }
    else
    {
        if(!rgb_mode)
        {
            cout << " * Radiometry enabled " << endl;
        }
        else
        {
            cout << " * Radiometry disabled " << endl;
        }
    }

    // NOTE: if radiometry is enabled is unuseful to keep AGC enabled
    //       (see "FLIR LEPTON 3® Long Wave Infrared (LWIR) Datasheet" for more info)

    if( lepton3->enableAgc( rgb_mode ) < 0)
    {
        cerr << "Failed to set radiometry status" << endl;
    }
    else
    {
        if(!rgb_mode)
        {
            cout << " * AGC disabled " << endl;
        }
        else
        {
            cout << " * AGC enabled " << endl;
        }
    }

    if( lepton3->enableRgbOutput( rgb_mode ) < 0 )
    {
        cerr << "Failed to enable RGB output" << endl;
    }
    else
    {
        if(rgb_mode)
        {
            cout << " * RGB enabled " << endl;
        }
        else
        {
            cout << " * RGB disabled " << endl;
        }
    }
}

cv::Mat normalizeFrame( const cv::Mat& frame16, uint16_t min, uint16_t max )
{
    cv::Mat tmp16;
    frame16.copyTo( tmp16);

    // >>>>> Rescaling/Normalization to 8bit
    double diff = static_cast<double>(max - min); // Image range
    double scale = 255./diff; // Scale factor

    tmp16 -= min; // Bias
    tmp16 *= scale; // Rescale data

    cv::Mat frame8;
    tmp16.convertTo( frame8, CV_8UC1 );
    // <<<<< Rescaling/Normalization to 8bit

    return frame8;
}

void mouseCallBackFunc(int event, int x, int y, int flags, void* userdata)
{
    if ( event == cv::EVENT_MOUSEMOVE )
    {
        //cout << "Mouse move over the window - position (" << x << ", " << y << ")" << endl;

        int raw_x = x/img_scale_fact;
        int raw_y = y/img_scale_fact;

        /*int w = frame16.rows;

        size_t idx = raw_x+raw_y*w;

        uint16_t* values = (uint16_t*)(&frame16.data[0]);
        uint16_t value = values[idx];*/

        uint16_t value = frame16.at<uint16_t>(raw_y, raw_x);

        double temp = value*scale_factor;

        std::cout << "Temp:" << temp << "- Raw: " << value << " [" << min_raw16 << "," << max_raw16 << "]" << std::endl;
    }
}
