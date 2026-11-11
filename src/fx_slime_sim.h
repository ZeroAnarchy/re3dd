#pragma once
// ---------------------------------------------------------------- fx 2026-11-11e/f: volumetric slime, the simulation
// Pure code (no game memory, no Direct3D): the live native sprites of one tick in, round-cone primitives in world units out.
// Used by the DLL (fx_nemesis_cloud.h slimeUpdate) and by the offline shots (qa/fx-2026-11-11f/slime/offline).
//
// What the original does (fx_nemesis_cloud.h, rounds 2 and 5): the living Nemesis throws the common blood sprites in his own
// palettes - 1700 (12 frames, at one point of a body part +-20 units: the bubbling blots at his hand), 0500 (falling drops: from
// the hand to the floor in ~23 frames), 0C00 (12 frames, flying 0.5-0.9 m) and 0D00 (12 frames, the splash of a drop on the
// floor); palette 4 purple, palette 2 grey-beige. After his first fall the hand drips all the time (pairs 1700 + 0500).
// Here every such sprite drives a piece of one slime body for as long as it lives:
//   1700 -> a bubble (a film: hollow) that swells out of the mass, wobbles and bursts when the sprite ends, throwing a few flecks;
//   0500 -> a falling drop with a thread behind it, tied to the mass (and to the drop before it) by strands that thin and snap;
//           it falls on until it meets the floor, also when its sprite ends in the air;
//   0C00 -> a flying gob with a short tail, shrinking;
//   0D00 -> a low frothy dome on the floor that spreads and sinks;
//   and while bubbles live on one body part, a core sits at the place they are born (the mean of their birth points in that
//   part's own space, so it rides the hand): liquid, with a lobe that hangs under it and swings behind when the hand moves;
//   the bubbles stand half sunk in its surface and small bubbles of its own keep rising between them.
// fx 2026-11-11f (user: "runnier, more transparent, with foam as in the original ... the drops on the ground are a good idea,
// better still if they left small puddles"): the primitives carry what they are made of (hollow = a bubble's film, froth, calm =
// a flat film) for the shader; whatever lands on the floor - a drop, a gob, a 0D00 splash - leaves a small flat puddle that
// stays for about twenty seconds, grows when more lands on it and then dries away.
// World units (1 = ~1 mm), Y down, one tick = 1/30 s.
#include <cmath>
#include <cstdint>
#include <vector>
#include <algorithm>
namespace re3slime {
using re3model::Vec3;
enum Kind:std::uint8_t{Splat=0,Drip=1,Spray=2,Floor=3};
struct Sprite {std::uint64_t id=0;Kind kind=Splat;std::uint8_t palette=4;Vec3 pos{};std::uint32_t emitter=0;};
// the body part the sprites of one emitter come from: its origin and axes (columns) now; valid = the matrix was readable.
// lift = where the mass sits relative to the place the sprites are born at (world units): the caller moves it out of the body
// toward the eye, as the native sprites are drawn over the body
struct Emitter {std::uint32_t key=0;bool valid=false;Vec3 origin{},axis[3]{{1,0,0},{0,1,0},{0,0,1}};float floorY=0;bool floorKnown=false;Vec3 lift{};};
// capR > 0: a film on the floor - only the cap of the sphere above floorY shows, capR = its radius there
struct Prim {Vec3 a{},b{};float ra=0,rb=0,mix=0,hollow=0,froth=0,calm=0,capR=0;std::uint64_t key=0;bool strand=false,floor=false;float floorY=0;};
struct Group {std::vector<Prim> now,before;bool floor=false;float floorY=0;};
struct Tuning {
    float bubbleMin=32,bubbleMax=66;                   // radius of a 1700 bubble
    float shellMin=0.9f,shellMax=1.3f;                 // how far from the core's centre the bubbles stand, in core radii
    float onMass=170;                                  // a 1700 sprite born nearer to the core than this is a bubble of the mass
    unsigned bubbles=10;                               // bubbles shown on one core at most (the other sprites only feed it)
    float core=110;                                    // radius of the core with three or more bubbles on it
    float coreLong=0.6f,coreWide=0.84f,coreFroth=0.16f; // the core is a capsule along its part: half length and radius in core radii; its froth
    float lobe=1.25f,lobeNeck=0.64f,lobeTip=0.2f;      // the hanging lobe: its length and radii in core radii
    float microMin=14,microMax=30;unsigned micro=4;    // the core's own small bubbles
    float dripMin=17,dripMax=38,dripTail=2.2f,dripTailMax=200,dripThread=0.2f;   // a drop: its head, the length and width of the thread behind it
    float strandReach=420,strand=0.36f,chain=240;      // a strand lives while the drop is nearer than this; its radius x the drop's; drop-to-drop strands
    float sprayMin=38,sprayMax=64;
    float floorMin=60,floorMax=104;                    // the splash of a 0D00 sprite
    float fleckMin=8,fleckMax=15;                      // what a bursting bubble throws
    float puddleMin=52,puddleMax=84,puddleGrown=150;   // radius of a new puddle, and the most it grows to when more lands on it
    unsigned puddleLife=660,puddleFade=150,puddles=36; // ticks it stays after the last drop, ticks it takes to dry, how many at once
    float link=26;                                     // primitives nearer than this (surface to surface) are one group
};
inline float Hash01(std::uint64_t id,std::uint32_t n){std::uint64_t h=id*0x9e3779b97f4a7c15ull+std::uint64_t(n)*0xbf58476d1ce4e5b9ull;h^=h>>31;h*=0x94d049bb133111ebull;h^=h>>29;return float(h&0xffffffu)/16777215.f;}
inline float Ease(float t){t=(std::min)(1.f,(std::max)(0.f,t));return t*t*(3.f-2.f*t);}
inline Vec3 OnSphere(std::uint64_t id,std::uint32_t n,float lift=0.2f){
    const float z=Hash01(id,n)*2-1,a=Hash01(id,n+1)*6.2831853f,r=std::sqrt((std::max)(0.f,1-z*z));Vec3 d{r*std::cos(a),z-lift,r*std::sin(a)};
    return re3model::Dot(d,d)>1e-4f?re3model::Normalize(d):Vec3{0,-1,0};
}
class Sim {
    struct Blob {std::uint64_t id=0;Kind kind=Splat;std::uint8_t palette=4;std::uint32_t emitter=0;Vec3 pos{},vel{},spawn{},centre{};float radius=0;std::uint32_t born=0,seen=0,released=0;bool live=true,onMass=false,shown=true,landed=false;};
    struct Core {std::uint32_t key=0;Vec3 local{},world{},tip{},along{};float samples=0,level=0,mix=0;std::uint32_t active=0;bool placed=false,tipSet=false;};
    struct Fleck {std::uint64_t id=0;Vec3 pos{},vel{};float radius=0,mix=0;std::uint32_t born=0;};
    struct Puddle {std::uint64_t id=0;Vec3 pos{};float size=0,target=0,mix=0;std::uint32_t born=0,fed=0;};
    std::vector<Blob> blobs_;std::vector<Core> cores_;std::vector<Fleck> flecks_;std::vector<Puddle> puddles_;std::vector<Prim> previous_;std::uint32_t tick_=0;std::uint64_t fleckId_=1,puddleId_=1;
    std::vector<Emitter> emitters_;
    const Emitter* emitter(std::uint32_t key)const{for(const auto& e:emitters_)if(e.key==key)return &e;return nullptr;}
    Core* core(std::uint32_t key){for(auto& c:cores_)if(c.key==key)return &c;return nullptr;}
    const Core* core(std::uint32_t key)const{for(const auto& c:cores_)if(c.key==key)return &c;return nullptr;}
    static Vec3 ToLocal(const Emitter& e,const Vec3& p){const Vec3 d=p-e.origin;return {re3model::Dot(d,e.axis[0]),re3model::Dot(d,e.axis[1]),re3model::Dot(d,e.axis[2])};}
    static Vec3 ToWorld(const Emitter& e,const Vec3& l){return e.origin+e.axis[0]*l.x+e.axis[1]*l.y+e.axis[2]*l.z;}
    float coreRadius(const Core& c)const{return tuning.core*c.level;}
    // a bubble of the mass: where it stands and how large it is this tick
    void placeBubble(Blob& b)const{
        const Tuning& t=tuning;const float age=float(tick_-b.born),gone=b.live?0.f:float(tick_-b.released);
        const float s0=Hash01(b.id,0),s1=Hash01(b.id,1);
        // swells in four ticks, wobbles, bursts in three
        const float swell=Ease(age/4.f),burst=b.live?1.f:(std::max)(0.f,1.f-gone/3.f);
        b.radius=(t.bubbleMin+(t.bubbleMax-t.bubbleMin)*s0*s0)*swell*(1.f+0.07f*std::sin(age*1.35f+6.28f*s1))*burst*std::sqrt(burst);
        const Vec3 dir=OnSphere(b.id,2,0.12f);const Core* c=core(b.emitter);
        // half sunk in the core's surface: it rises out of it while it swells
        if(b.onMass&&c)b.centre=c->world+dir*(t.core*(0.55f+0.45f*c->level)*(t.shellMin+(t.shellMax-t.shellMin)*Hash01(b.id,4))*(0.45f+0.55f*swell));
        else b.centre=b.pos+dir*(b.radius*0.25f);
    }
    // something landed on the floor: a new puddle, or more for the one that is there
    void land(const Vec3& at,float floorY,float amount,float mix){
        const Tuning& t=tuning;
        for(auto& p:puddles_){const float dx=p.pos.x-at.x,dz=p.pos.z-at.z,reach=p.target+34.f;
            if(std::fabs(p.pos.y-floorY)<80.f&&dx*dx+dz*dz<reach*reach){
                p.target=(std::min)(t.puddleGrown,std::sqrt(p.target*p.target+amount*amount*0.55f));p.fed=tick_;p.mix+=(mix-p.mix)*0.25f;
                // it creeps a little toward what fell beside it
                const float pull=0.12f;p.pos.x-=dx*pull;p.pos.z-=dz*pull;return;}}
        if(puddles_.size()>=t.puddles){   // the oldest dries now
            auto oldest=std::min_element(puddles_.begin(),puddles_.end(),[](const Puddle& a,const Puddle& b){return a.fed<b.fed;});
            if(tick_-oldest->fed<t.puddleLife)oldest->fed=tick_>t.puddleLife?tick_-t.puddleLife:0;}
        Puddle p;p.id=puddleId_++;p.pos={at.x,floorY,at.z};p.target=amount;p.size=amount*0.3f;p.mix=mix;p.born=p.fed=tick_;puddles_.push_back(p);
    }
public:
    Tuning tuning;
    std::uint32_t tick()const{return tick_;}
    bool empty()const{return blobs_.empty()&&cores_.empty()&&flecks_.empty()&&puddles_.empty();}
    std::size_t blobs()const{return blobs_.size();}
    std::size_t puddles()const{return puddles_.size();}
    void clear(){blobs_.clear();cores_.clear();flecks_.clear();puddles_.clear();previous_.clear();emitters_.clear();}
    // one tick: the sprites alive now (each with its place) and the parts they sit on
    void step(const std::vector<Sprite>& live,const std::vector<Emitter>& emitters){
        ++tick_;emitters_=emitters;const Tuning& t=tuning;
        // the cores ride their parts
        for(auto& c:cores_){const Emitter* e=emitter(c.key);if(e&&e->valid&&c.placed){c.world=ToWorld(*e,c.local)+e->lift;c.along=e->axis[1];}}
        for(const auto& s:live){
            Blob* blob=nullptr;for(auto& b:blobs_)if(b.id==s.id&&b.live){blob=&b;break;}
            if(!blob){blobs_.push_back({});blob=&blobs_.back();blob->id=s.id;blob->kind=s.kind;blob->palette=s.palette;blob->emitter=s.emitter;blob->pos=blob->spawn=s.pos;blob->born=tick_;
                const Emitter* e=emitter(s.emitter);
                if(s.kind==Splat||s.kind==Drip){Core* c=core(s.emitter);
                    if(!c){cores_.push_back({});c=&cores_.back();c->key=s.emitter;c->mix=s.palette==2?1.f:0.f;c->world=s.pos+(e?e->lift:Vec3{});}
                    const Vec3 away=s.pos+(e?e->lift:Vec3{})-c->world;
                    bool close=!c->placed||re3model::Dot(away,away)<t.onMass*t.onMass;
                    // born far from the mass (his tentacle throws them all along itself): when nothing of the mass lives at the old
                    // place any more, the mass is where this one is born
                    if(!close){bool alive=false;for(const auto& o:blobs_)if(o.emitter==s.emitter&&o.live&&o.onMass&&&o!=blob){alive=true;break;}
                        if(!alive){c->placed=false;c->samples=0;c->tipSet=false;c->level*=0.5f;close=true;}}
                    if(close){
                        // the place bubbles are born at, in the part's own space
                        const Vec3 local=e&&e->valid?ToLocal(*e,s.pos):s.pos;const float w=s.kind==Splat?1.f:0.35f;
                        c->samples=(std::min)(24.f,c->samples+w);c->local=c->placed?c->local+(local-c->local)*(w/c->samples):local;c->placed=true;
                        c->world=(e&&e->valid?ToWorld(*e,c->local):c->local)+(e?e->lift:Vec3{});
                        blob->onMass=true;
                        if(s.kind==Splat){unsigned shown=0;for(const auto& o:blobs_)if(o.kind==Splat&&o.onMass&&o.shown&&o.emitter==s.emitter&&o.live&&&o!=blob)++shown;blob->shown=shown<t.bubbles;}
                    }}
                // the splash of a drop: what it leaves on the floor
                if(s.kind==Floor){const float floorY=e&&e->floorKnown&&std::fabs(e->floorY-s.pos.y)<500.f?e->floorY:s.pos.y;
                    land(s.pos,floorY,t.puddleMin+(t.puddleMax-t.puddleMin)*Hash01(s.id,7),s.palette==2?1.f:0.f);}
            }
            blob->vel=s.pos-blob->pos;blob->pos=s.pos;blob->seen=tick_;
        }
        for(auto& b:blobs_)if(b.live&&b.seen!=tick_){b.live=false;b.released=tick_;
            // a bursting bubble throws a few flecks out, away from the core
            if(b.kind==Splat&&b.shown&&b.radius>12.f&&flecks_.size()<24){const Core* c=core(b.emitter);Vec3 out=c?b.centre-c->world:Vec3{0,-1,0};out=re3model::Dot(out,out)>1.f?re3model::Normalize(out):Vec3{0,-1,0};
                const unsigned count=2u+unsigned(Hash01(b.id,9)*1.99f);
                for(unsigned k=0;k<count;++k){Fleck f;f.id=fleckId_++;f.pos=b.centre+out*(b.radius*0.6f);
                    Vec3 dir=out+OnSphere(b.id,20+k*3,0.3f)*0.8f;dir=re3model::Normalize(dir);f.vel=dir*(12.f+20.f*Hash01(b.id,30+k));f.vel.y-=5.f;
                    f.radius=t.fleckMin+(t.fleckMax-t.fleckMin)*Hash01(b.id,40+k);f.mix=b.palette==2?1.f:0.f;f.born=tick_;flecks_.push_back(f);}}}
        // what falls meets the floor: a drop goes on falling after its sprite ends (the native one ends on the floor too)
        for(auto& b:blobs_){
            if(b.kind!=Drip&&b.kind!=Spray)continue;
            const Emitter* e=emitter(b.emitter);const bool floorKnown=e&&e->floorKnown&&b.spawn.y<e->floorY-30.f;
            if(!b.live&&!b.landed){b.vel.y+=10.5f;if(b.kind==Spray)b.vel=b.vel*0.96f;b.pos=b.pos+b.vel;}
            if(floorKnown&&!b.landed&&b.pos.y>=e->floorY-6.f){b.landed=true;if(!b.live)b.released=tick_;
                const float s0=Hash01(b.id,0);
                land(b.pos,e->floorY,b.kind==Drip?t.puddleMin*0.7f+(t.puddleMax-t.puddleMin)*s0:t.puddleMin+(t.puddleMax-t.puddleMin)*(0.5f+0.5f*s0),b.palette==2?1.f:0.f);}
        }
        blobs_.erase(std::remove_if(blobs_.begin(),blobs_.end(),[&](const Blob& b){
            if(b.live)return false;const std::uint32_t gone=tick_-b.released;
            if(b.kind==Floor)return gone>=7u;if(b.kind==Splat)return gone>=4u;
            if(b.landed)return true;
            const Emitter* e=emitter(b.emitter);const bool falls=b.kind==Drip&&e&&e->floorKnown&&b.spawn.y<e->floorY-30.f;
            return gone>=(falls?50u:3u);}),blobs_.end());
        for(auto& f:flecks_){f.vel.y+=10.5f;f.vel=f.vel*0.96f;f.pos=f.pos+f.vel;}
        flecks_.erase(std::remove_if(flecks_.begin(),flecks_.end(),[&](const Fleck& f){return tick_-f.born>9;}),flecks_.end());
        for(auto& p:puddles_)p.size+=(p.target-p.size)*0.16f;
        puddles_.erase(std::remove_if(puddles_.begin(),puddles_.end(),[&](const Puddle& p){return tick_-p.fed>=t.puddleLife+t.puddleFade;}),puddles_.end());
        for(auto& c:cores_){
            unsigned bubbles=0,drips=0;float mix=0;
            for(const auto& b:blobs_)if(b.emitter==c.key&&b.live&&b.onMass){if(b.kind==Splat){++bubbles;mix+=b.palette==2?1.f:0.f;}else if(b.kind==Drip)++drips;}
            const float target=bubbles?(std::min)(1.f,0.5f+float(bubbles)/5.f):drips?0.4f:0.f;
            c.level+=(target-c.level)*(target>c.level?0.35f:0.1f);if(bubbles){c.mix+=(mix/float(bubbles)-c.mix)*0.3f;c.active=tick_;}else if(drips)c.active=tick_;
            const Emitter* e=emitter(c.key);
            if(!(e&&e->valid)){Vec3 sum{};unsigned n=0;for(const auto& b:blobs_)if(b.emitter==c.key&&b.live&&b.kind==Splat&&b.onMass){sum=sum+b.pos;++n;}if(n)c.world=sum*(1.f/float(n))+(e?e->lift:Vec3{});}
            // the lobe that hangs under the core: its tip follows late, so it swings behind a moving hand and stretches as it falls
            const float r=coreRadius(c);const Vec3 want{c.world.x,c.world.y+r*(t.lobe+0.22f*std::sin(float(tick_)*0.19f+float(c.key&255u))),c.world.z};
            if(!c.tipSet){c.tip=want;c.tipSet=true;}else{c.tip=c.tip+(want-c.tip)*0.3f;
                const Vec3 d=c.tip-c.world;const float len=std::sqrt(re3model::Dot(d,d)),most=r*t.lobe*1.9f+20.f;if(len>most)c.tip=c.world+d*(most/len);}
        }
        cores_.erase(std::remove_if(cores_.begin(),cores_.end(),[&](const Core& c){return c.level<0.03f&&tick_-c.active>20;}),cores_.end());
        for(auto& b:blobs_)if(b.kind==Splat)placeBubble(b);
    }
    // the primitives of this tick, grouped: a group is one smooth body (at most `limit` primitives, the rest of a larger one
    // goes to a second group). `before` holds the same primitives a tick ago (new ones start at their own place, small).
    void build(std::vector<Group>& out,unsigned limit=24){
        out.clear();std::vector<Prim> prims;const Tuning& t=tuning;
        // the floor of the emitter being built: its primitives are cut at it (the downed Nemesis' hand lies on the floor - the mass is a
        // mound there, and a drop does not fall through the floor while its sprite still runs)
        bool floorNow=false;float floorNowY=0;
        // only for what was born above that floor: a floor reported above the hand would hide the whole mass
        auto useFloor=[&](std::uint32_t key,float bornY){const Emitter* e=emitter(key);floorNow=e&&e->floorKnown&&bornY<=e->floorY+120.f;floorNowY=e?e->floorY:0;};
        auto add=[&](const Vec3& a,float ra,const Vec3& b,float rb,float mix,std::uint64_t key,float hollow=0,float froth=0,bool strand=false)->Prim*{if(ra<1.f&&rb<1.f)return nullptr;Prim p;p.a=a;p.b=b;p.ra=(std::max)(ra,0.5f);p.rb=(std::max)(rb,0.5f);p.mix=mix;p.key=key;p.strand=strand;
            p.hollow=hollow;p.froth=froth;p.floor=floorNow;p.floorY=floorNowY;prims.push_back(p);return &prims.back();};
        for(const auto& c:cores_){
            const float r=coreRadius(c)*(1.f+0.05f*std::sin(float(tick_)*0.9f+float(c.key&255u)));
            if(r<6.f)continue;
            // along its part (the part's own Y axis runs down a limb): a short capsule over the hand, not a ball
            {const Vec3 half=c.along*(r*t.coreLong);useFloor(c.key,c.world.y);add(c.world-half,r*t.coreWide,c.world+half,r*t.coreWide*0.92f,c.mix,(std::uint64_t(c.key)<<8)|0xc0u,0.f,t.coreFroth);}
            // the hanging lobe
            if(c.tipSet){Vec3 d=c.tip-c.world;const float len=std::sqrt(re3model::Dot(d,d));
                if(len>r*0.5f)add(c.world+d*(r*0.35f/len),r*t.lobeNeck,c.tip,r*t.lobeTip*(0.8f+0.4f*(std::min)(1.f,r*t.lobe/len)),c.mix,(std::uint64_t(c.key)<<8)|0xc1u,0.f,0.08f);}
            // its own small bubbles: each slot swells and bursts in a period of its own, at a new place every time
            for(unsigned s=0;s<t.micro;++s){const unsigned period=6+s*2,phase=(tick_+s*5)%period;const std::uint64_t id=(std::uint64_t(c.key)<<20)^(std::uint64_t(s)<<16)^((tick_+s*5)/period);
                const float u=(float(phase)+0.5f)/float(period),size=std::sin(u*3.14159f)*(u<0.75f?1.f:(1.f-u)*4.f);
                const float r2=(t.microMin+(t.microMax-t.microMin)*Hash01(id,0))*size*c.level;
                const Vec3 at=c.world+OnSphere(id,1,0.3f)*(r*(0.9f+0.2f*Hash01(id,3))+r2*0.2f);
                add(at,r2,at,r2,c.mix,(std::uint64_t(c.key)<<8)|(0xd0u+s),0.8f,0.9f);}
        }
        const Blob* dripBefore=nullptr;
        for(const auto& b:blobs_){
            const float age=float(tick_-b.born),gone=b.live?0.f:float(tick_-b.released);const float mix=b.palette==2?1.f:0.f;
            const float s0=Hash01(b.id,0),s1=Hash01(b.id,1),s2=Hash01(b.id,2),s3=Hash01(b.id,3);
            const Core* c=core(b.emitter);const Emitter* e=emitter(b.emitter);useFloor(b.emitter,b.kind==Splat&&b.onMass&&c?c->world.y:b.spawn.y);
            switch(b.kind){
            case Splat:if(b.shown)add(b.centre,b.radius,b.centre,b.radius,mix,(b.id<<8)|1u,0.7f,0.62f);break;
            case Drip:{
                if(b.landed)break;
                // a drop with its thread up the way it came; strands to the core and to the drop before it while they are near
                const bool falls=e&&e->floorKnown&&b.spawn.y<e->floorY-30.f;
                const float shrink=b.live||falls?1.f:(std::max)(0.f,1.f-gone/3.f);
                const float r=(t.dripMin+(t.dripMax-t.dripMin)*s0*s0)*Ease((age+1.f)/3.f)*shrink;
                const float speed=std::sqrt(re3model::Dot(b.vel,b.vel));const Vec3 back=speed>0.5f?b.vel*(-1.f/speed):Vec3{0,-1,0};
                const Vec3 tail=b.pos+back*(std::min)(t.dripTailMax*(0.6f+0.4f*s1),speed*t.dripTail+r*0.6f);
                add(b.pos,r,b.pos+back*(r*0.5f),r*0.72f,mix,(b.id<<8)|2u);
                add(b.pos+back*(r*0.7f),r*t.dripThread*1.5f+1.f,tail,r*t.dripThread*0.6f+0.8f,mix,(b.id<<8)|10u,0.f,0.f,true);
                if(c&&c->level>0.2f&&b.live){const Vec3 from=c->tipSet?c->tip:c->world;Vec3 to=tail-from;const float d=std::sqrt(re3model::Dot(to,to));
                    if(d>12.f&&d<t.strandReach*(0.55f+0.45f*s2)){const float k=std::pow(1.f-d/(t.strandReach*(0.55f+0.45f*s2)),1.2f);
                        add(from,r*t.strand*k*1.6f+2.f,tail,r*t.strand*k+1.5f,mix,(b.id<<8)|3u,0.f,0.f,true);}}
                if(dripBefore&&dripBefore->emitter==b.emitter&&dripBefore->live&&!dripBefore->landed&&b.live){const Vec3 to=dripBefore->pos-tail;const float d=std::sqrt(re3model::Dot(to,to));
                    if(d>10.f&&d<t.chain){const float k=std::pow(1.f-d/t.chain,0.9f);add(tail,r*0.3f*k+1.5f,dripBefore->pos,r*0.3f*k+1.5f,mix,(b.id<<8)|8u,0.f,0.f,true);}}
                dripBefore=&b;break;}
            case Spray:{
                if(b.landed)break;
                const float r=(t.sprayMin+(t.sprayMax-t.sprayMin)*s0)*Ease((age+1.f)/2.f)*(1.f-0.45f*(std::min)(1.f,age/12.f))*(b.live?1.f:(std::max)(0.f,1.f-gone/3.f));
                const float speed=std::sqrt(re3model::Dot(b.vel,b.vel));const Vec3 back=speed>0.5f?b.vel*(-1.f/speed):Vec3{0,-1,0};
                add(b.pos,r,b.pos+back*(std::min)(110.f,speed*1.4f+r*0.4f),r*0.3f,mix,(b.id<<8)|4u,0.f,0.18f);
                // two smaller ones in its wake, a little off its path
                for(unsigned k=0;k<2;++k){const Vec3 off=OnSphere(b.id,50+k*2,0.f);const float rr=r*(k?0.34f:0.5f);
                    const Vec3 at=b.pos+back*((k?2.6f:1.5f)*r+(std::min)(120.f,speed*(k?1.6f:0.9f)))+off*(r*(k?1.3f:0.9f));add(at,rr,at+back*(rr*0.8f),rr*0.5f,mix,(b.id<<8)|(11u+k));}
                break;}
            case Floor:{
                // the splash: a frothy dome on the floor (the sphere's centre under the floor), it spreads, then sinks into its puddle
                const float floorY=e&&e->floorKnown&&std::fabs(e->floorY-b.pos.y)<500.f?e->floorY:b.pos.y;
                const float grow=0.5f+0.5f*Ease(age/4.f),sink=b.live?0.25f*Ease(age/12.f):0.25f+0.75f*Ease(gone/6.f);
                const float r=(t.floorMin+(t.floorMax-t.floorMin)*s0)*grow;
                const Vec3 centre{b.pos.x,floorY+r*(0.55f+0.7f*sink),b.pos.z};
                if(Prim* p=add(centre,r,centre,r,mix,(b.id<<8)|5u,0.f,0.3f)){p->floor=true;p->floorY=floorY;}
                for(unsigned k=0;k<2;++k){const float a=6.28f*(k?s2:s1),d=r*(0.75f+0.3f*s3),rr=r*(0.36f+0.16f*(k?s1:s2));
                    const Vec3 side{b.pos.x+std::cos(a)*d,floorY+rr*(0.5f+0.9f*sink),b.pos.z+std::sin(a)*d};if(Prim* p=add(side,rr,side,rr,mix,(b.id<<8)|(6u+k),0.6f,0.6f)){p->floor=true;p->floorY=floorY;}}
                break;}
            }
        }
        floorNow=false;
        for(const auto& f:flecks_){const float age=float(tick_-f.born),r=f.radius*(1.f-0.07f*age);const float speed=std::sqrt(re3model::Dot(f.vel,f.vel));
            const Vec3 back=speed>0.5f?f.vel*(-1.f/speed):Vec3{0,-1,0};add(f.pos,r,f.pos+back*(std::min)(r*1.6f,speed*0.4f),r*0.55f,f.mix,(f.id<<8)|9u);}
        // the puddles: a flat cap of a large sphere whose centre lies under the floor
        for(const auto& p:puddles_){
            const std::uint32_t idle=tick_-p.fed;const float dry=idle>t.puddleLife?1.f-Ease(float(idle-t.puddleLife)/float(t.puddleFade)):1.f;
            const float a=p.size*(0.55f+0.45f*dry),h=(3.5f+a*0.075f)*(0.35f+0.65f*dry)*(0.6f+0.4f*Ease(float(tick_-p.born)/6.f));
            if(a<6.f||h<0.8f)continue;
            const float R=(a*a+h*h)/(2.f*h);Prim q;q.a=q.b={p.pos.x,p.pos.y+(R-h),p.pos.z};q.ra=q.rb=R;q.mix=p.mix;q.key=(p.id<<8)|0x20u;q.calm=0.62f;q.froth=0.04f;q.capR=a;q.floor=true;q.floorY=p.pos.y;prims.push_back(q);
        }
        if(prims.empty()){previous_.clear();return;}
        // groups: primitives whose surfaces are nearer than `link` (strands do not join groups: each goes with what its first end touches)
        const std::size_t n=prims.size();std::vector<std::size_t> parent(n);for(std::size_t i=0;i<n;++i)parent[i]=i;
        auto find=[&](std::size_t i){while(parent[i]!=i){parent[i]=parent[parent[i]];i=parent[i];}return i;};
        // a film on the floor counts as its cap, not as the sphere under the floor
        auto ends=[&](const Prim& p,Vec3 pp[3],float pr[3]){
            if(p.capR>0){pp[0]=pp[1]=pp[2]={p.a.x,p.floorY,p.a.z};pr[0]=pr[1]=pr[2]=p.capR;return;}
            pp[0]=p.a;pp[1]=(p.a+p.b)*0.5f;pp[2]=p.b;pr[0]=p.ra;pr[1]=(p.ra+p.rb)*0.5f;pr[2]=p.rb;};
        auto gap=[&](const Prim& p,const Prim& q){   // segment to segment, coarse: ends and middles
            float best=1.e9f;Vec3 pp[3],qq[3];float pr[3],qr[3];ends(p,pp,pr);ends(q,qq,qr);
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){const Vec3 d=pp[i]-qq[j];best=(std::min)(best,std::sqrt(re3model::Dot(d,d))-pr[i]-qr[j]);}
            return best;};
        for(std::size_t i=0;i<n;++i){if(prims[i].strand)continue;for(std::size_t j=i+1;j<n;++j){if(prims[j].strand)continue;if(gap(prims[i],prims[j])<t.link)parent[find(i)]=find(j);}}
        for(std::size_t i=0;i<n;++i){if(!prims[i].strand)continue;float best=1.e9f;std::size_t to=i;
            for(std::size_t j=0;j<n;++j){if(prims[j].strand||prims[j].capR>0)continue;const Vec3 d=prims[i].a-prims[j].a;const float g=std::sqrt(re3model::Dot(d,d))-prims[j].ra;if(g<best){best=g;to=j;}}
            parent[i]=find(to);}
        std::vector<std::size_t> roots;for(std::size_t i=0;i<n;++i){const std::size_t r=find(i);if(std::find(roots.begin(),roots.end(),r)==roots.end())roots.push_back(r);}
        auto beforeOf=[&](const Prim& p){for(const auto& q:previous_)if(q.key==p.key)return q;Prim q=p;if(!p.strand&&p.capR<=0){q.ra*=0.3f;q.rb*=0.3f;}return q;};
        // a film is the largest sphere but the least of a mass: the noise rides the first primitive, so the films go last
        auto weight=[&](const Prim& p){return p.capR>0?0.f:p.ra;};
        for(const std::size_t top:roots){
            std::vector<std::size_t> members;for(std::size_t i=0;i<n;++i)if(find(i)==top)members.push_back(i);
            while(!members.empty()){
                // the largest first, then the nearest to it
                std::size_t lead=0;for(std::size_t k=1;k<members.size();++k)if(weight(prims[members[k]])>weight(prims[members[lead]]))lead=k;
                Vec3 lp[3];float lr[3];ends(prims[members[lead]],lp,lr);const Vec3 at=lp[0];const std::size_t first=members[lead];
                std::sort(members.begin(),members.end(),[&](std::size_t x,std::size_t y){if(x==first||y==first)return x==first&&y!=first;
                    Vec3 xp[3],yp[3];float xr[3],yr[3];ends(prims[x],xp,xr);ends(prims[y],yp,yr);const Vec3 dx=xp[0]-at,dy=yp[0]-at;return re3model::Dot(dx,dx)<re3model::Dot(dy,dy);});
                const std::size_t take=(std::min<std::size_t>)(limit,members.size());Group g;
                for(std::size_t k=0;k<take;++k){const Prim& p=prims[members[k]];g.now.push_back(p);g.before.push_back(beforeOf(p));if(p.floor&&!g.floor){g.floor=true;g.floorY=p.floorY;}}
                members.erase(members.begin(),members.begin()+take);out.push_back(std::move(g));
            }
        }
        previous_=prims;
    }
};
} // namespace re3slime

// ---------------------------------------------------------------- the HD body round the place the sprites are born at
// The native sprites are born inside the body (at a part's origin +-20 units) and drawn over it; a mass there would be hidden
// in the HD body. BodyPoints: the mesh's vertices near `local` in the part's own space (bind positions through the part's
// inverse bind matrix). BodyReach: how far the body reaches from `local` along `dir` - the farthest of those points near that line.
namespace re3slime {
inline void BodyPoints(const re3model::Mesh& mesh,std::size_t part,const Vec3& local,std::vector<Vec3>& out,float within=440.f){
    out.clear();if(part>=mesh.inverseBind.size())return;const auto& toPart=mesh.inverseBind[part];
    for(const auto& v:mesh.vertices){const Vec3 p=re3model::TransformPoint(toPart,v.position),d=p-local;if(re3model::Dot(d,d)<within*within)out.push_back(p);}
}
inline float BodyReach(const std::vector<Vec3>& points,const Vec3& local,const Vec3& dir,float beside=55.f,float most=340.f){
    float best=0;
    for(const auto& p:points){const Vec3 d=p-local;const float s=re3model::Dot(d,dir);if(s<=best)continue;if(re3model::Dot(d,d)-s*s<beside*beside)best=s;}
    return (std::min)(best,most);
}
} // namespace re3slime

// ---------------------------------------------------------------- world group -> camera-space batch (needs model_gpu.h before this file)
namespace re3slime {
// the native light of an actor (rows toward each light x strength, colour matrix, back colour, base colour), into the camera's space
inline void SetLight(re3gpu::SlimeBatch& out,const re3model::NativeLighting* light,const re3model::Matrix3x4& camera,float floorLevel=0.4f){
    const float* c=camera.m;
    for(unsigned i=0;i<3;++i){
        float row[3]={0,0,0},colour[3]={0,0,0};
        if(light){for(unsigned k=0;k<3;++k){row[k]=float(light->worldLight[i*3+k])/4096.f;colour[k]=float(light->color[k*3+i])/4096.f*float(light->baseRGB[k])/128.f;}}
        for(unsigned r=0;r<3;++r)out.lightDir[i][r]=c[r*4]*row[0]+c[r*4+1]*row[1]+c[r*4+2]*row[2];
        for(unsigned k=0;k<3;++k)out.lightColour[i][k]=(std::max)(0.f,colour[k]);
    }
    for(unsigned k=0;k<3;++k)out.ambient[k]=(std::max)(floorLevel,light?float(light->ambient[k])/4096.f*float(light->baseRGB[k])/128.f:0.5f);
    // no light rows at all (a cut scene's black room): a soft light from above the eye so the shape still reads
    float total=0;for(unsigned i=0;i<3;++i)total+=std::fabs(out.lightDir[i][0])+std::fabs(out.lightDir[i][1])+std::fabs(out.lightDir[i][2]);
    if(total<0.05f){out.lightDir[0][0]=-0.35f;out.lightDir[0][1]=-0.6f;out.lightDir[0][2]=-0.55f;for(unsigned k=0;k<3;++k)out.lightColour[0][k]=0.55f;}
}
// fx 2026-11-11f2 (user: "it turned out well, but can this slime be made darker?"): the tones are about two thirds of what they
// were (purple 0.12 0.04 0.18 / 0.36 0.14 0.47 / 0.62 0.38 0.76, grey-beige 0.19 0.165 0.115 / 0.48 0.44 0.34 / 0.72 0.68 0.56),
// the froth and the film of a bubble are less white and the body absorbs more of the picture behind it (model_gpu.h).
// brightness: pilot.ini nemesis_slime_brightness / 100 (1 = this look; the glints stay as they are)
inline void DefaultTones(re3gpu::SlimeBatch& out,float brightness=1.f){
    // deep / body / thin; the purple and the grey-beige of the native palettes (TEXU 9 and 5), a little richer
    static const float tones[6][4]={{0.075f,0.022f,0.115f,0},{0.245f,0.085f,0.335f,0},{0.4f,0.215f,0.52f,0},{0.12f,0.105f,0.072f,0},{0.32f,0.29f,0.22f,0},{0.48f,0.45f,0.37f,0}};
    std::memcpy(out.tones,tones,sizeof(tones));
    brightness=(std::max)(0.3f,(std::min)(2.f,brightness));
    out.look2[3]=brightness;out.look[3]=re3gpu::SlimeBatch().look[3]/brightness;   // a darker one lets less of the picture through
}
// false: nothing of the group is in front of the near plane
inline bool MakeBatch(const Group& g,const re3model::Projection& pr,float viewportWidth,float viewportHeight,std::uint32_t tick,re3gpu::SlimeBatch& out){
    const std::size_t count=(std::min<std::size_t>)(g.now.size(),re3gpu::kSlimeMaxPrims);if(!count)return false;
    out.now.resize(count);out.before.resize(count);
    // the smooth union swells where primitives overlap (log(n) / sharpness at most), the noise lifts the surface
    const float swell=std::log(float(count)+1.f)/out.sharpness*0.6f+out.noise+out.look2[2]+5.f;
    float rect[4]={1.e9f,1.e9f,-1.e9f,-1.e9f};bool any=false;
    // a sphere's screen rectangle into `rect` (the whole view when it reaches the eye)
    auto cover=[&](const float* s,float radius){
        if(s[2]+radius<=pr.nearPlane)return;any=true;
        const float z=s[2]-radius;
        if(z<=pr.nearPlane*1.5f){rect[0]=rect[1]=0;rect[2]=viewportWidth;rect[3]=viewportHeight;return;}
        const float sx=(s[0]*pr.h/s[2]+pr.offsetX)*pr.scaleX+pr.pixelOffsetX,sy=(s[1]*pr.h/s[2]+pr.offsetY)*pr.scaleY+pr.pixelOffsetY;
        // the silhouette of a sphere is wider than its radius at its centre's depth: the radius at its near side, and the centre's drift
        const float k=pr.h/z,rx=radius*k*pr.scaleX*1.05f+2.f+std::fabs(s[0])*(k-pr.h/s[2])*pr.scaleX,ry=radius*k*pr.scaleY*1.05f+2.f+std::fabs(s[1])*(k-pr.h/s[2])*pr.scaleY;
        rect[0]=(std::min)(rect[0],sx-rx);rect[1]=(std::min)(rect[1],sy-ry);rect[2]=(std::max)(rect[2],sx+rx);rect[3]=(std::max)(rect[3],sy+ry);};
    auto fill=[&](const std::vector<Prim>& from,std::vector<re3gpu::SlimePrim>& to,float bound[4]){
        Vec3 sum{};float weight=0;std::vector<Vec3> centres(count);std::vector<float> radii(count);
        for(std::size_t i=0;i<count;++i){const Prim& p=from[i];const Vec3 a=re3model::TransformPoint(pr.camera,p.a),b=re3model::TransformPoint(pr.camera,p.b);
            to[i].a[0]=a.x;to[i].a[1]=a.y;to[i].a[2]=a.z;to[i].a[3]=p.ra;to[i].b[0]=b.x;to[i].b[1]=b.y;to[i].b[2]=b.z;to[i].b[3]=p.rb;
            to[i].c[0]=p.mix;to[i].c[1]=p.hollow;to[i].c[2]=p.froth;to[i].c[3]=p.calm;
            if(p.capR>0){   // a film on the floor: its cap only
                const Vec3 cap=re3model::TransformPoint(pr.camera,{p.a.x,p.floorY,p.a.z});const float s[3]={cap.x,cap.y,cap.z},reach=p.capR+out.noise*0.6f+8.f;
                cover(s,reach);centres[i]=cap;radii[i]=reach;sum=sum+cap*p.capR;weight+=p.capR;continue;}
            // a strand is thin: only as wide as itself on screen
            const float grow=p.strand?(std::min)(swell,(std::max)(p.ra,p.rb)*2.f+4.f):swell;
            cover(to[i].a,p.ra+grow);cover(to[i].b,p.rb+grow);
            if(p.strand){const float mid[3]={(a.x+b.x)*0.5f,(a.y+b.y)*0.5f,(a.z+b.z)*0.5f};cover(mid,(p.ra+p.rb)*0.5f+grow);}
            centres[i]=(a+b)*0.5f;{const Vec3 half=(a-b)*0.5f;radii[i]=std::sqrt(re3model::Dot(half,half))+(std::max)(p.ra,p.rb)+swell;}
            sum=sum+a*p.ra+b*p.rb;weight+=p.ra+p.rb;}
        const Vec3 centre=sum*(1.f/(std::max)(weight,1e-3f));float radius=0;
        for(std::size_t i=0;i<count;++i){const Vec3 d=centres[i]-centre;radius=(std::max)(radius,std::sqrt(re3model::Dot(d,d))+radii[i]);}
        bound[0]=centre.x;bound[1]=centre.y;bound[2]=centre.z;bound[3]=radius;};
    fill(g.now,out.now,out.bound);fill(g.before.size()>=count?g.before:g.now,out.before,out.boundBefore);
    if(!any)return false;
    std::memcpy(out.rect,rect,sizeof(rect));
    out.nearPlane=pr.nearPlane;out.time=float(tick)/30.f;out.timeBefore=float(tick)/30.f-1.f/30.f;
    out.ray[0]=1.f/(pr.h*pr.scaleX);out.ray[1]=-(pr.pixelOffsetX/pr.scaleX+pr.offsetX)/pr.h;out.ray[2]=1.f/(pr.h*pr.scaleY);out.ray[3]=-(pr.pixelOffsetY/pr.scaleY+pr.offsetY)/pr.h;
    const float* c=pr.camera.m;
    for(unsigned r=0;r<3;++r)out.up[r]=-c[r*4+1];   // world up (0,-1,0) in the camera's space
    if(g.floor){   // above the floor: y < floorY (Y down). n = R (0,-1,0); n.p + w with p = R x + t
        for(unsigned r=0;r<3;++r)out.floorPlane[r]=-c[r*4+1];
        out.floorPlane[3]=g.floorY-(out.floorPlane[0]*c[3]+out.floorPlane[1]*c[7]+out.floorPlane[2]*c[11]);
    }else{out.floorPlane[0]=out.floorPlane[1]=out.floorPlane[2]=0;out.floorPlane[3]=1.e6f;}
    return true;
}
} // namespace re3slime
