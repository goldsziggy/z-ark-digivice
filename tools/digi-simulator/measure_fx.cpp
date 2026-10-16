// Host renderer overhead only: not ESP32 frame timing or an FPS prediction.
#include "device_ui.hpp"
#include <chrono>
#include <cstdio>
#include <vector>
using namespace digivice;
int main(){
 auto s=newGame(12345);if(apply(s,Action::WorldSeed,12345)!=Error::None||apply(s,Action::Walk,100)!=Error::None)return 2;
 deviceui::Model m;m.writable=true;m.starterStage=onboarding::Stage::Complete;
 deviceui::Controller ui;ui.update(s,m);std::vector<std::uint16_t> pixels(deviceui::kPixels);
 for(unsigned effect=0;effect<=5;++effect){
  const auto start=std::chrono::steady_clock::now();
  for(unsigned frame=0;frame<1000;++frame){m.digiModify={effect!=0,static_cast<std::uint16_t>(frame),static_cast<digimodify::Effect>(effect),2};
   if(!ui.render(s,m,pixels.data(),pixels.size(),frame))return 3;}
  const auto us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count();
  std::printf("effect=%u render_mean_us=%.2f frames=1000 host_only=true art=fallback\n",effect,double(us)/1000);
 }
}
