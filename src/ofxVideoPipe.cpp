#include "ofxVideoPipe.h"

/*******************************************************************************
 * Headers
 ******************************************************************************/

ofxVideoPipe::PPMHeader::PPMHeader() :
channels(3), width(0), height(0), depth(0), headerSize(0)
{}

ofxVideoPipe::PPMHeader::PPMHeader(PPMHeader &mom){
    type = mom.type;
    width = mom.width;
    height = mom.height;
    channels = mom.channels;
    depth = mom.depth;
    error << mom.error.str();
    headerSize = mom.headerSize;
}

void ofxVideoPipe::PPMHeader::reset(){
    type = "";
    width = 0;
    height = 0;
    channels = 3;
    depth = 0;
    error.str("");
    headerSize = 0;
}

int ofxVideoPipe::PPMHeader::dataSize(){
    return width * height * channels;
}

bool ofxVideoPipe::PPMHeader::good(){
    return error.str().empty();
}

/*******************************************************************************
 * Frames
 ******************************************************************************/

void ofxVideoPipe::PPMFrame::writeTo(ofPixels & pixels){
    if(header.width < 1 || header.height < 1) return;
    pixels.setFromPixels(
                         (unsigned char *)getBinaryBuffer(),
                         header.width,
                         header.height,
                         header.channels
                         );
}

void ofxVideoPipe::PPMFrame::reset(){
    header.reset();
}

/*******************************************************************************
 * Video Pipe Class
 ******************************************************************************/

/*******************************************************************************
 * Threading
 */

void ofxVideoPipe::threadedFunction(){
    readFrame(true);
    while(isThreadRunning()){
        readFrame();
        idle();
    }
}

/*******************************************************************************
 * Timing
 *
 * much of this section is cribbed from OF core's ofSetFrameRate
 */
void ofxVideoPipe::idleNoThread(){
    if(!bthread) readFrame();
}

void ofxVideoPipe::idle(){
    if (ofGetFrameNum() != 0 && isFrameRateSet == true){
		int diffMillis = ofGetElapsedTimeMillis() - prevMillis;
		if (diffMillis > millisForFrame){
			; // we do nothing, we are already slower than target frame
		} else {
			int waitMillis = millisForFrame - diffMillis;
#ifdef TARGET_WIN32
            Sleep(waitMillis);         //windows sleep in milliseconds
#else
            usleep(waitMillis * 1000);   //mac sleep in microseconds - cooler :)
#endif
		}
	}
	prevMillis = ofGetElapsedTimeMillis(); // you have to measure here
}

void ofxVideoPipe::setFrameRate(float targetRate){
    // given this FPS, what is the amount of millis per frame
    // that should elapse?

    // --- > f / s

    if (targetRate == 0){
        isFrameRateSet = false;
        return;
    }

    isFrameRateSet 			= true;
    durationOfFrame 	    = 1.0f / (float)targetRate;
    millisForFrame 			= (int)(1000.0f * durationOfFrame);
}

void ofxVideoPipe::setUseTexture(bool useTexture){
    mUseTexture = useTexture;
}
/*******************************************************************************
 * Update
 */

void ofxVideoPipe::update(){
    isFrameChanged = false;

    lock();
    bool changed = isPixelsChanged;
    unlock();
    if(!changed) return;


    int oldWidth = pixels.getWidth();
    int oldHeight = pixels.getHeight();

    updatePixels();

    int w = pixels.getWidth();
    int h = pixels.getHeight();
    if((oldWidth != w || oldHeight != h) && (w > 0 && h > 0)){
        onSizeChangedData data(w, h);
        ofNotifyEvent(onSizeChanged, data, this);
    }

    if(mUseTexture) frameImage.setFromPixels(getPixelsRef());

    lock();
    isPixelsChanged = false;
    unlock();
    isFrameChanged = true;
}

ofPixelsRef ofxVideoPipe::getPixelsRef(){
    return pixels;
}

void ofxVideoPipe::updatePixels(){
    lock();
    if(isPixelsChanged){
        currentFrame.writeTo(pixels);
    }
    unlock();
}

bool ofxVideoPipe::isFrameNew(){
    return isFrameChanged;
}

/*******************************************************************************
 * Draw
 */

void ofxVideoPipe::draw(int x, int y){
    if(mUseTexture) frameImage.draw(x, y);
}

void ofxVideoPipe::draw(int x, int y, int w, int h){
    if(mUseTexture) frameImage.draw(x, y, w , h);
}
/*******************************************************************************
 * Opening, closing
 */

void ofxVideoPipe::open(string _filename){
    filename = _filename;
    if(mUseTexture) frameImage.allocate(1, 1, OF_IMAGE_COLOR);
    if(bthread){
        if (openPipe() == OPEN_PIPE_SUCCESS) startThread();
    }
    else readFrame(true);
}

int ofxVideoPipe::openPipe(){
    if (isPipeOpen) return OPEN_PIPE_SUCCESS;

    if (filename.empty()) {
        ofLogError() << "Could not open pipe because it is not properly initialized.";
        return OPEN_PIPE_INIT_FAIL;
    }
    if(mUseTexture){
        if (!frameImage.bAllocated()) {
            ofLogError() << "Could not open pipe because it is not properly initialized.";
            return OPEN_PIPE_INIT_FAIL;
        }
    }



    // lots of voodoo to avoid deadlocks when the fifo writer goes away
    // temporarily.

    int fd_pipe = ::open(ofToDataPath(filename).c_str(), O_RDONLY | O_NONBLOCK);
    if (fd_pipe < 0) {
        ofLogError() << "Error opening pipe " << filename << ": " << errno;
        return OPEN_PIPE_FD_FAIL;
    }

    fd_set set;
    FD_ZERO(&set);
    FD_SET(fd_pipe, &set);

    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    int ready = select(fd_pipe + 1, &set, NULL, NULL, &timeout);
    ::close(fd_pipe);

    if (ready < 0) {
        ofLogError() << "Error waiting for pipe to be ready for reading.";
        return OPEN_PIPE_SELECT_FAIL;
    } else if (ready == 0) {
        // timeout
        return OPEN_PIPE_TIMEOUT;
    }

    pipe.open(filename, ofFile::ReadOnly, true);

    // get length of file:
    pipe.seekg (0, pipe.end);
    lenghtofFile = pipe.tellg();
    pipe.seekg (0, pipe.beg);
    cout<<"lenghtofFile"<<lenghtofFile<<endl;


    isPipeOpen = true;
    return OPEN_PIPE_SUCCESS;
}

void ofxVideoPipe::closePipe(){
    if (!isPipeOpen) return;

    pipe.close();
    isPipeOpen = false;
}

void ofxVideoPipe::close(){
    waitForThread(true);
    closePipe();
}

/*******************************************************************************
 * Reading data
 */

void ofxVideoPipe::readFrame(bool first) throw() {
    if(openPipe() != OPEN_PIPE_SUCCESS) return;

    lock();

    try {
        currentFrame.reset();

        readHeader();

        char * buffer = new char[currentFrame.dataSize()];
        pipe.read(buffer, currentFrame.dataSize());
        handlePipeReadError();

        currentFrame.set(buffer);
        delete buffer;
        isPixelsChanged = true;

        // get current frame
        if (sizeoframe != 0) {
            int position = pipe.tellg();
            currentFrameIndex =  position / sizeoframe ;
            //cout<<currentFrameIndex<<", ";
        }

        if (first) {
            sizeofheader = currentFrame.header.headerSize;
            sizeofdata = currentFrame.dataSize();
            sizeoframe = sizeofheader + sizeofdata;
            nbframe = lenghtofFile / sizeoframe ;
            videoduration = nbframe * durationOfFrame;
            cout<<"sizeofheader "<<sizeofheader<<endl;
            cout<<"sizeofdata "<<sizeofdata<<endl;
            cout<<"sizeoframe "<<sizeoframe<<endl;
            cout<<"nbframe "<<nbframe<<endl;
            cout<<"videoduration "<<videoduration<<endl;
        }
    } catch (ReadError & boom) {
        if(!currentFrame.good()){
            ofLogError() << "Error opening PPM stream for reading: " << currentFrame.errors();
        }
    }

    unlock();
}

void ofxVideoPipe::setFrameForTime(float time) {
    lock();
    int index = fmod(time,videoduration) / durationOfFrame;
    unlock();
    setFrame(index);
}

void ofxVideoPipe::setFrame(unsigned int index) throw() {
    if(openPipe() != OPEN_PIPE_SUCCESS) return;

    lock();

    try {
        unsigned int position = index * sizeoframe;
        if(position  <= lenghtofFile - sizeoframe) pipe.seekg(position);
    } catch (...) {
        ofLogError() << "Error seeking PPM stream: " << currentFrame.errors();
    }
    unlock();
}

string ofxVideoPipe::readLine(int & linesize ) throw(ofxVideoPipe::ReadError) {
    string buffer;
    getline(pipe, buffer);
    handlePipeReadError();
    linesize = buffer.size()+1;
    return buffer;
}

void ofxVideoPipe::readHeader() throw(ofxVideoPipe::ReadError) {
    PPMHeader & header = currentFrame.header;

    int currentheadersize = 0;
    int sizereadline;

    header.type = readLine(sizereadline);
    currentheadersize += sizereadline;

    if(header.type != "P6"){
        header.error << "PPM type identifier not found in header.";
        throw readError;
    }

    istringstream dimensions(readLine(sizereadline));
    currentheadersize += sizereadline;
    dimensions >> header.width;
    dimensions >> header.height;
    if(header.width < 1 || header.height < 1){
        header.error << "Invalid dimensions for PPM stream. " << header.width << header.height;
        throw readError;
    }

    header.depth = ofToInt(readLine(sizereadline));
    currentheadersize += sizereadline;

    header.headerSize = currentheadersize;
}

void ofxVideoPipe::handlePipeReadError() throw(ofxVideoPipe::ReadError) {
    if (pipe.good()) return;

    if (pipe.eof()) {
        ofLogError() << "Pipe has been closed, attempting to reopen.";
        closePipe();
    } else if (pipe.fail()) {
        ofLogError() << "Reading from pipe failed.";
    }
    throw readError;
}
