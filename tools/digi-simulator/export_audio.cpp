// Export the exact original firmware oscillator cues, never sampled assets.
#include "audio_cues.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
using namespace digivice::device;
void word(FILE* f,unsigned value,unsigned bytes){for(unsigned i=0;i<bytes;++i)std::fputc((value>>(8*i))&255,f);}
int main(int argc,char** argv){
 if(argc!=2)return 2;
 for(auto cue:{AudioCue::DigiCritical,AudioCue::DigiPower,AudioCue::DigiGuard,AudioCue::DigiRecovery,AudioCue::DigiElement,AudioCue::DigiCriticalHit}){
  CueSynth synth;synth.start(cue);std::vector<std::int16_t> pcm;
  while(!synth.finished())pcm.push_back(synth.sample(100));
  auto path=std::string(argv[1])+"/"+cueName(cue)+".wav";auto* f=std::fopen(path.c_str(),"wb");if(!f)return 3;
  std::fwrite("RIFF",1,4,f);word(f,36+pcm.size()*2,4);std::fwrite("WAVEfmt ",1,8,f);word(f,16,4);word(f,1,2);word(f,1,2);
  word(f,kAudioSampleRate,4);word(f,kAudioSampleRate*2,4);word(f,2,2);word(f,16,2);std::fwrite("data",1,4,f);word(f,pcm.size()*2,4);
  for(auto p:pcm)word(f,static_cast<std::uint16_t>(p),2);
  std::fclose(f);std::printf("%s: %zu samples\n",cueName(cue),pcm.size());
 }
}
