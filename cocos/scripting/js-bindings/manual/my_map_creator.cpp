//
//  my_map_creator.cpp
//  cocos2d_js_bindings
//
//  Created by luleyan on 2018/12/12.
//

#include "my_map_creator.hpp"
#include <thread>

#include "scripting/js-bindings/auto/jsb_cocos2dx_auto.hpp"
#include "scripting/js-bindings/manual/jsb_conversions.hpp"
#include "scripting/js-bindings/manual/jsb_global.h"
#include "cocos2d.h"
#include "scripting/js-bindings/manual/BaseJSAction.h"

#define MY_SPACE_BEGIN   namespace my {
#define MY_SPACE_END     }

USING_NS_CC;

// 地图生成器 ----------------------------------------------------------------

MY_SPACE_BEGIN

// 需要整数的x, y
class MapVec {
public:
    MapVec();
    virtual ~MapVec();

    int x;
    int y;
};

// 随机区域中使用的区块模板
class MapEle {
public:
    MapEle();
    virtual ~MapEle();

    int w;
    int h;
    std::vector<std::vector<int>> te; // 地形
    std::vector<std::vector<int>> co; // 碰撞
};

// 区块模板对应大小和门的配置
class MapEleConfig {
public:
    MapEleConfig();
    virtual ~MapEleConfig();

    int index; // ele序号
    MapVec pos; // 对应元素中的位置作为起始位置

    std::vector<std::vector<MapVec*>> door; // 连接方向
}

// 模板中的固定块
class FiTemp {
public:
    FiTemp();
    virtual ~FiTemp();

    int x;
    int y;
    int w;
    int h;

    std::vector<std::vector<int>> te; // 地形
    std::vector<std::vector<int>> co; // 碰撞
    std::vector<std::vector<MapVec*>> door; // 固定块的连接方向
};

// 地图模板
class MapTemp {
public:
    MapTemp();
    virtual ~MapTemp();

    int w;
    int h;

    std::vector<int> noeps; // 无敌人位置
    std::vector<FiTemp*> fis; // 固定块
    std::vector<std::vector<int>> ra; // 随机块
};

class MapData {
    MapData();
    virtual ~MapData();
};

class MapCreator {
public:
    MapCreator();
    virtual ~MapCreator();

    static MapCreator* getInstance();

    // 载入区块元素
    void addMapEle(const MapEle* mapEle);

    // 读取模板，生成地图，然后从回调传出
    void createMap(const MapTemp* mapBase, const std::function<void(MapData*)>& callback);

protected:
    void init();

    void threadLoop();

private:
    bool _creating;

    std::thread _thread;
    std::mutex _sleepMutex;
    std::condition_variable _sleepCondition;

    std::vector<const MapEle*> _mapEleVec;
    MapEleConfig _mapEleConfigs[6][6]; // 对应不同w，h的随机区块使用的配置，最大宽高是固定的

    MapTemp* _mapBase;
    std::function<void(MapData*)> _callback;

    MapData* _mapData;
};

// 实现 --------------------------------------------------------------

MapVec::MapVec() {
}

MapVec::~MapVec() {
}

// ---------------

MapEle::MapEle() {
}

MapEle::~MapEle() {
}

// ---------------

MapEleConfig::MapEleConfig() {
}

MapEleConfig::~MapEleConfig() {
    for (auto vec : door) {
        for (MapVec* mapvec: vec) {
            delete mapvec;
        }
    }
}

// ---------------

FiTemp::FiTemp() {
}

FiTemp::~FiTemp() {
    for (auto vec : door) {
        for (MapVec* mapvec: vec) {
            delete mapvec;
        }
    }
}

// ---------------

MapTemp::MapTemp() {
}

MapTemp::~MapTemp() {
    for (FiTemp* fi: fis) {
        delete fi;
    }
}

// ---------------

MapData::MapData() {
}

MapData::~MapData() {
}

// ---------------

static MapCreator *s_MapCreator = nullptr;

MapCreator::MapCreator():
_creating(false),
_mapBase(nullptr) {

}

MapCreator::~MapCreator() {
    for (MapEle* ele: _mapEleVec) {
        delete ele;
    }
    if (_mapBase) delete _mapBase;
    if (_mapData) delete _mapData;
}

MapCreator* MapCreator::getInstance() {
    if (!s_MapCreator) {
        s_MapCreator = new (std::nothrow) MapCreator();
        s_MapCreator->init();
    }
    return s_MapCreator;
}

void MapCreator::addMapEle(const MapEle* mapEle) {
    _mapEleVec.push_back(mapEle);
}

void MapCreator::createMap(const MapTemp* mapBase, const std::function<void(MapData*)>& callback) {
    if (_creating) return;
    _creating = true;

    if (_mapBase != nullptr) {
        delete _mapBase;
    }

    if (_mapData != nullptr) {
        delete _mapData;
        _mapData = nullptr;
    }

    _mapBase = const_cast<MapTemp*>(mapBase);
    _callback = callback;

    std::unique_lock<std::mutex> lk(_sleepMutex);
    _sleepCondition.notify_one();
}

void MapCreator::init() {
    // 初始化线程
    _thread = std::thread(&MapCreator::threadLoop, this);
    _thread.detach();
}

void MapCreator::threadLoop() {
    while (true) {
        std::unique_lock<std::mutex> lk(_sleepMutex);
        _sleepCondition.wait(lk);

        log("hahahahahaha");

        // 结束
        auto sch = cocos2d::Director::getInstance()->getScheduler();
        sch->performFunctionInCocosThread([=]() {
            _creating = false;
            _callback(_mapData);
        });
    }
}

MY_SPACE_END

// js绑定工具 ----------------------------------------------------------------

using namespace my;

bool seval_to_vecvec(const se::Value& v, std::vector<std::vector<int>>* ret) {
    assert(ret != nullptr);
    assert(v.isObject());
    se::Object* obj = v.toObject();
    assert(obj->isArray());

    bool ok = true;
    uint32_t len = 0;
    ok = obj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error vecvec len");

    se::Value tmp;
    for (uint32_t i = 0; i < len; ++i) {
        ok = obj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error vecvec tmp");

        se::Object* subobj = tmp.toObject();
        assert(subobj->isArray());

        uint32_t sublen = 0;
        ok = subobj->getArrayLength(&sublen);
        SE_PRECONDITION2(ok, false, "error vecvec sublen");

        std::vector<int> subVec;
        se::Value subtmp;
        for (uint32_t j = 0; j < sublen; ++j) {
            ok = subobj->getArrayElement(j, &subtmp);
            SE_PRECONDITION2(ok && subtmp.isNumber(), false, "error vecvec num");

            int num = subtmp.toInt32();
            subVec.push_back(num);
        }

        ret->push_back(subVec);
    }

    return true;
}

bool vecvec_to_seval(const std::vector<std::vector<int>>& v, se::Value* ret) {
    return true;
}

bool seval_to_mapele(const se::Value& v, MapEle* ret) {
    return true;
}

bool seval_to_fitemp(const se::Value& v, FiTemp* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value x;
    se::Value y;
    se::Value w;
    se::Value h;
    se::Value te;
    se::Value co;
    se::Value door;

    bool ok;
    uint32_t len = 0;
    se::Value tmp;

    // w and h x y
    ok = obj->getProperty("x", &x);
    SE_PRECONDITION2(ok && x.isNumber(), false, "error x");
    ret->x = x.toInt32();

    ok = obj->getProperty("y", &y);
    SE_PRECONDITION2(ok && y.isNumber(), false, "error y");
    ret->y = y.toInt32();

    ok = obj->getProperty("w", &w);
    SE_PRECONDITION2(ok && w.isNumber(), false, "error w");
    ret->w = w.toInt32();

    ok = obj->getProperty("h", &h);
    SE_PRECONDITION2(ok && h.isNumber(), false, "error h");
    ret->h = h.toInt32();

    // te co
    ok = obj->getProperty("te", &te);
    SE_PRECONDITION2(ok, false, "error te");

    ok = seval_to_vecvec(te, &ret->te);
    SE_PRECONDITION2(ok, false, "error te res");

    ok = obj->getProperty("co", &co);
    SE_PRECONDITION2(ok, false, "error co");

    ok = seval_to_vecvec(co, &ret->co);
    SE_PRECONDITION2(ok, false, "error co res");

    // door
    ok = obj->getProperty("door", &door);
    SE_PRECONDITION2(ok && door.isObject(), false, "error door");
    se::Object* doorobj = door.toObject();
    assert(doorobj->isArray());
    ok = doorobj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error door len");

    for (uint32_t i = 0; i < len; ++i) {
        ok = doorobj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isObject(), false, "error door tmp");

        se::Object* subobj = tmp.toObject();
        assert(subobj->isArray());

        uint32_t sublen = 0;
        ok = subobj->getArrayLength(&sublen);
        SE_PRECONDITION2(ok, false, "error door sublen");

        std::vector<MapVec*> subVec;
        se::Value subtmp;
        for (uint32_t j = 0; j < sublen; ++j) {
            ok = subobj->getArrayElement(j, &subtmp);
            SE_PRECONDITION2(ok && subtmp.isObject(), false, "error door sub tmp");

            MapVec* v2 = new MapVec();

            se::Object* v2obj = subtmp.toObject();
            se::Value xx;
            se::Value yy;
            ok = v2obj->getProperty("x", &xx);
            SE_PRECONDITION2(ok && xx.isNumber(), false, "error door x");
            ok = obj->getProperty("y", &yy);
            SE_PRECONDITION2(ok && yy.isNumber(), false, "error door y");
            v2->x = xx.toInt32();
            v2->y = yy.toInt32();

            subVec.push_back(v2);
        }

        ret->door.push_back(subVec);
    }

    return true;
}

bool seval_to_maptemp(const se::Value& v, MapTemp* ret) {
    assert(v.isObject() && ret != nullptr);
    se::Object* obj = v.toObject();

    se::Value w;
    se::Value h;
    se::Value noeps;
    se::Value fis;
    se::Value ra;

    bool ok;
    uint32_t len = 0;
    se::Value tmp;

    // w and h
    ok = obj->getProperty("w", &w);
    SE_PRECONDITION2(ok && w.isNumber(), false, "error w");
    ret->w = w.toInt32();

    ok = obj->getProperty("h", &h);
    SE_PRECONDITION2(ok && h.isNumber(), false, "error h");
    ret->h = h.toInt32();

    // noeps
    ok = obj->getProperty("noeps", &noeps);
    SE_PRECONDITION2(ok && noeps.isObject(), false, "error noeps");
    se::Object* noepsObj = noeps.toObject();
    assert(noepsObj->isArray());

    ok = noepsObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error noeps len");

    for (uint32_t i = 0; i < len; ++i) {
        ok = noepsObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok && tmp.isNumber(), false, "error noeps obj");
        ret->noeps.push_back(tmp.toInt32());
    }

    // fis
    ok = obj->getProperty("fis", &fis);
    SE_PRECONDITION2(ok && fis.isObject(), false, "error fis");
    se::Object* fisObj = fis.toObject();
    assert(fisObj->isArray());

    ok = fisObj->getArrayLength(&len);
    SE_PRECONDITION2(ok, false, "error fisObj len");

    for (uint32_t i = 0; i < len; ++i) {
        ok = fisObj->getArrayElement(i, &tmp);
        SE_PRECONDITION2(ok, false, "error fisObj obj");

        FiTemp* fiTemp = new FiTemp();
        ok = seval_to_fitemp(tmp, fiTemp);
        SE_PRECONDITION2(ok, false, "error fisObj fiTemp");

        ret->fis.push_back(fiTemp);
    }

    // ra
    ok = obj->getProperty("ra", &ra);
    SE_PRECONDITION2(ok, false, "error ra");

    ok = seval_to_vecvec(ra, &ret->ra);
    SE_PRECONDITION2(ok, false, "error ra res");

    return true;
}

bool mapdata_to_seval(const MapData* v, se::Value* ret) {
    return true;
}

// js绑定 -------------------------------------------------------------------

se::Object* __jsb_my_MapCreator_proto = nullptr;
se::Class* __jsb_my_MapCreator_class = nullptr;

static bool jsb_my_MapCreator_getInstance(se::State& s)
{
    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 0) {
        auto result = MapCreator::getInstance();
        se::Value instanceVal;
        native_ptr_to_seval<MapCreator>(result, __jsb_my_MapCreator_class, &instanceVal);
        instanceVal.toObject()->root();
        s.rval() = instanceVal;
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 0);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_getInstance)

static bool jsb_my_MapCreator_addMapEle(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEle : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 1) {
        MapEle* arg0 = new MapEle();
        ok &= seval_to_mapele(args[0], arg0);
        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_addMapEle : Error processing arguments");

        cobj->addMapEle(arg0);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 1);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_addMapEle);

static bool jsb_my_MapCreator_create(se::State& s) {
    MapCreator* cobj = (MapCreator*)s.nativeThisObject();
    SE_PRECONDITION2(cobj, false, "jsb_my_MapCreator_addMapEle : Invalid Native Object");

    const auto& args = s.args();
    size_t argc = args.size();
    CC_UNUSED bool ok = true;
    if (argc == 2) {
        MapTemp* arg0 = new MapTemp();
        std::function<void(MapData*)> arg1 = nullptr;

        ok &= seval_to_maptemp(args[0], arg0);

        do {
            if (args[1].isObject() && args[1].toObject()->isFunction()) {
                se::Value jsThis(s.thisObject());
                se::Value jsFunc(args[1]);
                jsThis.toObject()->attachObject(jsFunc.toObject());
                auto lambda = [=](MapData* larg0) -> void {
                    se::ScriptEngine::getInstance()->clearException();
                    se::AutoHandleScope hs;

                    CC_UNUSED bool ok = true;
                    se::ValueArray rargs;
                    rargs.resize(1);
                    ok &= mapdata_to_seval(larg0, &rargs[0]);
                    se::Value rval;
                    se::Object* thisObj = jsThis.isObject() ? jsThis.toObject() : nullptr;
                    se::Object* funcObj = jsFunc.toObject();
                    bool succeed = funcObj->call(rargs, thisObj, &rval);
                    if (!succeed) {
                        se::ScriptEngine::getInstance()->clearException();
                    }
                };
                arg1 = lambda;
            } else {
                ok = false;
            }
        } while(false);

        SE_PRECONDITION2(ok, false, "jsb_my_MapCreator_create : Error processing arguments");

        cobj->createMap(arg0, arg1);
        return true;
    }
    SE_REPORT_ERROR("wrong number of arguments: %d, was expecting %d", (int)argc, 2);
    return false;
}
SE_BIND_FUNC(jsb_my_MapCreator_create)

bool register_my_map_creator(se::Object* obj) {
    // 命名空间
    se::Value nsVal;
    if (!obj->getProperty("my", &nsVal))
    {
        se::HandleObject jsobj(se::Object::createPlainObject());
        nsVal.setObject(jsobj);
        obj->setProperty("my", nsVal);
    }
    se::Object* ns = nsVal.toObject();

    // 生成类
    auto cls = se::Class::create("MapCreator", ns, nullptr, nullptr);

    cls->defineStaticFunction("getInstance", _SE(jsb_my_MapCreator_getInstance));
    cls->defineFunction("addMapEle", _SE(jsb_my_MapCreator_addMapEle));
    cls->defineFunction("createMap", _SE(jsb_my_MapCreator_create));
    cls->install();
    JSBClassType::registerClass<MapCreator>(cls);

    __jsb_my_MapCreator_proto = cls->getProto();
    __jsb_my_MapCreator_class = cls;

    se::ScriptEngine::getInstance()->clearException();
    return true;
}
