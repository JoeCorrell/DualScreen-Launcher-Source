#include "../src/WeaponModel.h"
#include <iostream>
#include <chrono>
int main(int argc,char** argv){
    if(argc!=3)return 2;
    c3::WeaponModel model;
    if(!model.Load(argv[1])){std::cerr<<"Rejected model\n";return 3;}
    const auto start=std::chrono::steady_clock::now();
    const auto image=model.Render(900,300);
    std::cout<<"Offline mesh render ms: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()<<"\n";
    if(image.rgba.empty())return 4;
    std::ofstream output(argv[2],std::ios::binary);
    const std::array<uint32_t,4> header{0x41523343,1,image.width,image.height};
    output.write(reinterpret_cast<const char*>(header.data()),16);
    output.write(reinterpret_cast<const char*>(image.rgba.data()),std::streamsize(image.rgba.size()));
    return output?0:5;
}
