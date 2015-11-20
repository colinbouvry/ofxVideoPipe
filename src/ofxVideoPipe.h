#pragma once

#include "ofMain.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

class ofxVideoPipe : public ofThread {
    class PPMHeader {
    public:
        PPMHeader();
        PPMHeader(PPMHeader &other);
        int dataSize();
        bool good();
        void reset();
        string type;
        int width;
        int height;
        int depth;
        int channels;
        stringstream error;
        int headerSize;
    };

    class PPMFrame : public ofBuffer {
    public:
        PPMFrame(){};
        void reset();
        void set(const char * data){ ofBuffer::set(data, dataSize()); }
        void writeTo(ofPixels & pixels);
        int dataSize(){ return header.dataSize(); }
        bool good(){ return header.good(); }
        string errors(){ return header.error.str(); }
        int getWidth(){ return header.width; }
        int getHeight(){ return header.height; }
        PPMHeader header;
    };


public:
    struct onSizeChangedData {
        onSizeChangedData(int w, int h) : width(w), height(h) {};
        int width;
        int height;
    };

    class ReadError : public std::runtime_error {
    public:
        ReadError() : std::runtime_error("Error reading from pipe") {};
        ReadError (const string& message) : std::runtime_error(message) {};
    } readError;

    ofxVideoPipe() : isFrameChanged(false), isPipeOpen(false), filename(""), bthread(true), currentFrameIndex(0), sizeoframe(0), mUseTexture(true) {};

    void open(string _filename);
    void close();
    void draw(int x, int y);
    void draw(int x, int y, int w, int h);
    void threadedFunction();
    void update();

    bool isFrameNew();
    ofPixelsRef getPixelsRef();
    void updatePixels();
    void setFrameRate(float targetRate);
    void setUseTexture(bool useTexture);

    int getWidth(){ return currentFrame.getWidth(); }
    int getHeight(){ return currentFrame.getHeight(); }
    unsigned int getCurrentFrame(){
        lock();
        unsigned int ret = currentFrameIndex;
        unlock();
        return ret;
    }
    unsigned int getTotalNumFrames(){
        lock();
        unsigned int ret = nbframe;
        unlock();
        return ret;
    }

    ofEvent< onSizeChangedData > onSizeChanged;

    void setThread(bool thread) { bthread = thread;}
    void setFrame(unsigned int index) throw();
    void setFrameForTime(float time);
    void idleNoThread();
private:
    void readFrame (bool first = false) throw();
    string readLine( int & linesize  ) throw(ReadError);
    void readHeader() throw(ReadError);

    void idle();

    int openPipe();
    void closePipe();

    //PPMFrame referenceFrame;
    PPMFrame currentFrame;
    string lastLine;
    ofPixels pixels;
    ofImage frameImage;
    ofFile pipe;
    string filename;
    bool isFrameChanged;
    bool isPixelsChanged;
    bool isFrameRateSet;
    bool isPipeOpen;
    int prevMillis, millisForFrame;
    bool bthread;
    //infos general
    unsigned int lenghtofFile;
    unsigned int nbframe;
    unsigned int sizeofheader;
    unsigned int sizeofdata;
    unsigned int sizeoframe;
    float durationOfFrame;
    float videoduration;
    unsigned int currentFrameIndex;
    bool mUseTexture;

    enum openPipeResult {
        OPEN_PIPE_SUCCESS = 0,
        OPEN_PIPE_INIT_FAIL = -1,
        OPEN_PIPE_FD_FAIL = -2,
        OPEN_PIPE_SELECT_FAIL = -3,
        OPEN_PIPE_TIMEOUT = -4
    };

    void handlePipeReadError() throw(ReadError);
};
