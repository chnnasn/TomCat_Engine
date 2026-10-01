#pragma once
#include <array>
#include <map>
#include <string>
#include <string_view>
#include <vector>
namespace TomCat {
struct AudioBusSend { uint32_t Target=0;float Gain=1; };
struct AudioBus { uint32_t ID=0;std::string Name;float Volume=1;bool Muted=false,Solo=false;std::vector<AudioBusSend> Sends; };
class AudioBusGraph {
public:
    AudioBusGraph();
    bool Configure(std::string_view document,std::string& error);
    bool Set(uint32_t id,float volume,bool muted,bool solo);
    bool Contains(uint32_t id) const { return m_Buses.contains(id); }
    float Gain(uint32_t id,const std::array<float,3>& volumes,const std::array<bool,3>& muted,const std::array<bool,3>& solo) const;
    const std::map<uint32_t,AudioBus>& Buses() const {return m_Buses;}
private:
    std::map<uint32_t,AudioBus> m_Buses;
};
}
