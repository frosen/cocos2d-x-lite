//
//  my_map_creator.hpp
//  cocos2d_js_bindings
//
//  Created by luleyan on 2018/12/12.
//

#ifndef my_map_creator_hpp
#define my_map_creator_hpp

#include "base/ccConfig.h"
#include "cocos/scripting/js-bindings/jswrapper/SeApi.h"

namespace se {
    class Object;
    class Class;
}

extern se::Object* __jsb_my_MapCreator_proto;
extern se::Class* __jsb_my_MapCreator_class;

bool register_my_map_creator(se::Object* obj);

SE_DECLARE_FUNC(jsb_my_MapCreator_getInstance);
SE_DECLARE_FUNC(jsb_my_MapCreator_addMapEleBase);
SE_DECLARE_FUNC(jsb_my_MapCreator_addMapEle);
SE_DECLARE_FUNC(jsb_my_MapCreator_addMapEleIndexs);
SE_DECLARE_FUNC(jsb_my_MapCreator_addMapTemp);
SE_DECLARE_FUNC(jsb_my_MapCreator_createMap);

#endif /* my_map_creator_hpp */
