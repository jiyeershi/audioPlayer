#ifndef __AudioPlayer__
#define __AudioPlayer__

#include <string>
#include <queue>
#include <mutex>
#include "cocos2d.h"
#include "RubberBandStretcher.h"
#include "sndfile.h"

struct audioDataFrame {
    float* data;
    int size;
};

class WanakaAudioPlayer;
class AudioPlayer {
public:
    AudioPlayer();
    virtual ~AudioPlayer();

    //static AudioPlayer* getInstance();

    //���ſ���
    void play(const char* audioPath, float timeScale = 1.f, bool isLoop = false); //��ʼ���Ż�ȡ����ͣ isLoopΪtrue��ѭ������
    void pause();
    void resume();
    void stop();
    void seek(float second);
    bool isPlaying() { return _isPlaying; }

    float getDuration();  //��ȡ��ʱ������λ���룩
    float getCurrent();   //��ȡ��ǰ�����˵�ʱ������λ���룩
	void setVolume(float v); //v�ķ�ΧΪ0-1

    void setDelegate(WanakaAudioPlayer* delegate){ _delegate = delegate; };
private:
    bool initAudio(const char* audioPath);
    bool initAudioContext();
    void strechProcess();
    void seekProcess();
    void timeCalculate(); //����ʱ���ۻ�����

    //��ձ������ݻ���
    void reset();
    void clearCache();

    //��Ƶ�߳�
    friend/* static */void processAudio(AudioPlayer* player, uint8_t* stream, int len);

private:
    bool _isPlaying;
    bool _quit;
    bool _isLoop;
    bool _audioOk;
    bool _isStrechProcessing;
    bool _isSeek;
	bool _isFinished;
    double _seekSecond;
    double _elapsedTime;
    double _totalTime;
	float _volume;
    int _totalDataSize;
    int _skipDataSize; //���������촦�����ڽ���ʱ������seek������������������, ��λ��sizeof(float)
    std::string _fullPath;
    float _timeScale;
    RubberBand::RubberBandStretcher* _rubberStrecher;
    std::vector<audioDataFrame> _audioDataVec;
    std::mutex _lock;
    WanakaAudioPlayer* _delegate;
};

#endif /* defined(__AudioPlayer__) */