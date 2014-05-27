#ifndef _LIVEMEDIA_HH
#include <liveMedia/liveMedia.hh>
#endif

#include <string>

#ifndef _HANDLERS_HH
#include "../src/network/Handlers.hh"
#endif

#ifndef _SOURCE_MANAGER_HH
#include "../src/network/SourceManager.hh"
#endif

#include "../src/AudioFrame.hh"
#include "../src/modules/audioDecoder/AudioDecoderLibav.hh"
#include "../src/modules/audioEncoder/AudioEncoderLibav.hh"
#include "../src/modules/audioMixer/AudioMixer.hh"
#include "../src/AudioCircularBuffer.hh"

#include <iostream>
#include <csignal>


#define PROTOCOL "RTP"
#define PAYLOAD 97
#define BANDWITH 5000

#define A_CODEC "opus"
#define A_CLIENT_PORT1 6006
#define A_CLIENT_PORT2 6008
#define A_MEDIUM "audio"
#define A_TIME_STMP_FREQ 48000
#define A_CHANNELS 2

#define CHANNEL_MAX_SAMPLES 3000
#define OUT_CHANNELS 2
#define OUT_SAMPLE_RATE 48000

bool should_stop = false;

struct buffer {
    unsigned char* data;
    int data_len;
};

void signalHandler( int signum )
{
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    
    SourceManager *mngr = SourceManager::getInstance();
    mngr->closeManager();
    
    std::cout << "Manager closed\n";
}

void fillBuffer(struct buffer *b, Frame *pFrame) {
    memcpy(b->data + b->data_len, pFrame->getDataBuf(), pFrame->getLength());
    b->data_len += pFrame->getLength(); 
}

void saveBuffer(struct buffer *b) 
{
    FILE *audioChannel = NULL;
    char filename[32];

    sprintf(filename, "coded.opus");

    audioChannel = fopen(filename, "wb");

    if (b->data != NULL) {
        fwrite(b->data, b->data_len, 1, audioChannel);
    }

    fclose(audioChannel);
}

int main(int argc, char** argv) 
{   
    std::string sessionId;
    std::string sdp;
    Session* session;
    SourceManager *mngr = SourceManager::getInstance();
    AudioDecoderLibav* audioDecoder1;
    AudioDecoderLibav* audioDecoder2;
    AudioEncoderLibav* audioEncoder;
    Worker *audioDecoder1Worker;
    Worker *audioDecoder2Worker;
    Worker *audioEncoderWorker;
    Worker *audioMixerWorker;
    std::map<unsigned short, FrameQueue*> inputs;
    FrameQueue* q1;
    FrameQueue* q2;
    AudioCircularBuffer* audioCirBuffer1;
    AudioCircularBuffer* audioCirBuffer2;
    Frame* aFrame;
    PlanarAudioFrame* destinationPlanarFrame;
    Frame *codedFrame;
    struct buffer *buffers;

    ACodecType inCType = OPUS;
    ACodecType outCType = PCM;
    SampleFmt outSFmt = S16P;
    unsigned int bytesPerSample = 2;

    
    signal(SIGINT, signalHandler); 
    
    for (int i = 1; i <= argc-1; ++i) {
        sessionId = handlers::randomIdGenerator(ID_LENGTH);
        session = Session::createNewByURL(*(mngr->envir()), argv[0], argv[i]);
        mngr->addSession(sessionId, session);
    }
    
    sessionId = handlers::randomIdGenerator(ID_LENGTH);
    
    sdp = handlers::makeSessionSDP("testSession", "this is a test");
    
    sdp += handlers::makeSubsessionSDP(A_MEDIUM, PROTOCOL, PAYLOAD, A_CODEC, BANDWITH, 
                                        A_TIME_STMP_FREQ, A_CLIENT_PORT1, A_CHANNELS);
    sdp += handlers::makeSubsessionSDP(A_MEDIUM, PROTOCOL, PAYLOAD, A_CODEC, BANDWITH, 
                                        A_TIME_STMP_FREQ, A_CLIENT_PORT2, A_CHANNELS);
    
    session = Session::createNew(*(mngr->envir()), sdp, sessionId);
    
    mngr->addSession(session);

    session->initiateSession();
       
    mngr->runManager();
       
    audioDecoder1 = new AudioDecoderLibav();
    audioDecoder2 = new AudioDecoderLibav();

    audioEncoder = new AudioEncoderLibav();
    audioEncoder->configure(PCMU);

    AudioMixer *mixer = new AudioMixer(4);

    audioDecoder1Worker = new Worker(audioDecoder1);
    audioDecoder2Worker = new Worker(audioDecoder2);
    audioEncoderWorker = new Worker(audioEncoder);
    audioMixerWorker = new Worker(mixer);

    if(!audioDecoder1->connect(audioDecoder1->getAvailableWriters().front(), mixer, mixer->getAvailableReaders().front())) {
        std::cerr << "Error connecting audio decoder 1 with mixer" << std::endl;
    }

    if(!audioDecoder2->connect(audioDecoder2->getAvailableWriters().front(), mixer, mixer->getAvailableReaders().front())) {
        std::cerr << "Error connecting audio decoder 2 with mixer" << std::endl;
    }

    if(!mixer->connect(mixer->getAvailableWriters().front(), audioEncoder, audioEncoder->getAvailableReaders().front())) {
        std::cerr << "Error connecting mixer with encoder" << std::endl;
    }

    if(!mngr->connect(mngr->getAvailableWriters().front(), audioDecoder1, audioDecoder1->getAvailableReaders().front())) {
        std::cerr << "Error connecting audio decoder 2 with mixer" << std::endl;
    }

    if(!mngr->connect(mngr->getAvailableWriters().front(), audioDecoder2, audioDecoder2->getAvailableReaders().front())) {
        std::cerr << "Error connecting mixer with encoder" << std::endl;
    }

    inputs = mngr->getInputs();
    q1 = inputs[A_CLIENT_PORT1];
    q2 = inputs[A_CLIENT_PORT2];

    buffers = new struct buffer;
    buffers->data = new unsigned char[CHANNEL_MAX_SAMPLES * bytesPerSample * OUT_SAMPLE_RATE * 360]();
    buffers->data_len = 0;
    
    audioDecoder1->getReader(0)->setConnection(q1);
    audioDecoder2->getReader(0)->setConnection(q2);
    
    Reader *reader = new Reader();
    audioEncoder->connect(audioEncoder->getAvailableWriters().front(), reader);

    audioDecoder1Worker->start(); 
    audioDecoder2Worker->start(); 
    audioEncoderWorker->start(); 
    audioMixerWorker->start(); 

    while(mngr->isRunning()) {
        codedFrame = reader->getFrame();

        if (!codedFrame) {
            usleep(1000);
            continue;
        }

        fillBuffer(buffers, codedFrame);
        printf("Filled buffer! Frame size: %d\n", codedFrame->getLength());

        reader->removeFrame();
    }

    saveBuffer(buffers);
    printf("Buffer saved\n");

    return 0;
}