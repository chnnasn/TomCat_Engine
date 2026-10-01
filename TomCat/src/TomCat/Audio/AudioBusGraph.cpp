#include "tcpch.h"
#include "AudioBusGraph.h"
#include <yaml-cpp/yaml.h>
#include <set>
#include <functional>
namespace TomCat {
AudioBusGraph::AudioBusGraph() {m_Buses={{0,{0,"Master",1,false,false,{}}},{1,{1,"Music",1,false,false,{{0,1}}}},{2,{2,"SFX",1,false,false,{{0,1}}}}};}
bool AudioBusGraph::Set(uint32_t id,float volume,bool muted,bool solo) {
    auto found=m_Buses.find(id);if(found==m_Buses.end() || !std::isfinite(volume) || volume<0 || volume>4) return false;
    found->second.Volume=volume;found->second.Muted=muted;found->second.Solo=solo;return true;
}
bool AudioBusGraph::Configure(std::string_view document,std::string& error) {
    error.clear();if(document.size()>65536) {error="audio graph exceeds 64 KiB";return false;}
    try {
        auto root=YAML::Load(std::string(document));
        auto fields=[](const YAML::Node& node,std::initializer_list<const char*> allowed) {
            if(!node.IsMap()) throw std::runtime_error("audio graph record must be a map");
            std::set<std::string> seen;
            for(auto item:node) {auto key=item.first.as<std::string>();if(!seen.insert(key).second || std::none_of(allowed.begin(),allowed.end(),[&](auto value){return key==value;})) throw std::runtime_error("unknown or duplicate audio graph field: "+key);}
        };
        fields(root,{"SchemaVersion","Buses"});if(root["SchemaVersion"].as<unsigned>()!=1) throw std::runtime_error("unsupported audio graph schema");
        auto nodes=root["Buses"];if(!nodes.IsSequence() || nodes.size()<3 || nodes.size()>128) throw std::runtime_error("audio graph requires 3..128 buses");
        std::map<uint32_t,AudioBus> candidate;std::set<std::string> names;
        for(auto node:nodes) {
            fields(node,{"ID","Name","Volume","Muted","Solo","Sends"});AudioBus bus;bus.ID=node["ID"].as<uint32_t>();bus.Name=node["Name"].as<std::string>();
            bus.Volume=node["Volume"].as<float>(1);bus.Muted=node["Muted"].as<bool>(false);bus.Solo=node["Solo"].as<bool>(false);
            if(bus.ID==UINT32_MAX || bus.Name.empty() || bus.Name.size()>128 || !names.insert(bus.Name).second || !std::isfinite(bus.Volume) || bus.Volume<0 || bus.Volume>4) throw std::runtime_error("invalid audio bus identity or volume");
            auto sends=node["Sends"];if(sends && (!sends.IsSequence() || sends.size()>16)) throw std::runtime_error("audio bus sends must be a sequence of at most 16");
            std::set<uint32_t> targets;
            if(sends) for(auto send:sends) {fields(send,{"Target","Gain"});AudioBusSend edge{send["Target"].as<uint32_t>(),send["Gain"].as<float>(1)};if(!std::isfinite(edge.Gain) || edge.Gain<0 || edge.Gain>4 || !targets.insert(edge.Target).second) throw std::runtime_error("invalid or duplicate audio send");bus.Sends.push_back(edge);}
            if(!candidate.emplace(bus.ID,std::move(bus)).second) throw std::runtime_error("duplicate audio bus ID");
        }
        if(!candidate.contains(0) || !candidate.contains(1) || !candidate.contains(2) || !candidate.at(0).Sends.empty()) throw std::runtime_error("buses 0/1/2 are required and Master 0 must be the sink");
        std::map<uint32_t,int> state;
        std::function<void(uint32_t)> visit=[&](uint32_t id) {if(!candidate.contains(id)) throw std::runtime_error("missing audio send target");if(state[id]==1) throw std::runtime_error("audio routing cycle");if(state[id]==2) return;state[id]=1;const auto& bus=candidate.at(id);if(id && bus.Sends.empty()) throw std::runtime_error("audio bus does not reach Master");for(auto edge:bus.Sends)visit(edge.Target);state[id]=2;};
        for(const auto& [id,bus]:candidate) visit(id);
        m_Buses=std::move(candidate);return true;
    } catch(const std::exception& e) {error=e.what();return false;}
}
float AudioBusGraph::Gain(uint32_t id,const std::array<float,3>& volumes,const std::array<bool,3>& muted,const std::array<bool,3>& solo) const {
    if(!Contains(id)) return 0;
    bool anySolo=std::any_of(solo.begin(),solo.end(),[](bool v){return v;});for(const auto& [key,bus]:m_Buses)anySolo|=bus.Solo;
    std::map<std::pair<uint32_t,bool>,double> memo;
    std::function<double(uint32_t,bool)> gain=[&](uint32_t key,bool selected) -> double {
        const auto& bus=m_Buses.at(key);selected|=bus.Solo || (key<3 && solo[key]);const auto cacheKey=std::pair{key,selected};
        if(auto found=memo.find(cacheKey);found!=memo.end())return found->second;
        if(bus.Muted || (key<3 && muted[key]))return 0;
        double route=key==0 ? ((!anySolo || selected)?1:0) : 0;
        for(auto edge:bus.Sends) route+=edge.Gain*gain(edge.Target,selected);
        const double value=route*bus.Volume*(key<3 ? volumes[key] : 1);
        return memo[cacheKey]=std::min(value,65536.0);
    };
    return static_cast<float>(gain(id,false));
}
}
