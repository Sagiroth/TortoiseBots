#pragma once

#include "runtime/PlayerbotAIStorage.h"

// Value lookup macros are shared by the inline value/trigger headers.  Keeping
// them independent of AiObjectContext.h avoids an include-order dependency in
// the native module build.
#define AI_VALUE(type, name) context->GetValue<type>(name)->Get()
#define AI_VALUE2(type, name, param) context->GetValue<type>(name, param)->Get()

#define AI_VALUE_SAFE(type, name) context->GetValue<type>(name) ? context->GetValue<type>(name)->Get() : type()
#define AI_VALUE2_SAFE(type, name, param) context->GetValue<type>(name, param) ? context->GetValue<type>(name, param)->Get() : type()

#define AI_VALUE_LAZY(type, name) context->GetValue<type>(name)->LazyGet()
#define AI_VALUE2_LAZY(type, name, param) context->GetValue<type>(name, param)->LazyGet()

#define HAS_AI_VALUE(name) context->HasValue(name)
#define HAS_AI_VALUE2(name, param) context->HasValue(name, param)
#define AI_VALUE_EXISTS(type, name, emptyval) (HAS_AI_VALUE(name) ? AI_VALUE(type, name) : emptyval)
#define AI_VALUE2_EXISTS(type, name, param, emptyval) (HAS_AI_VALUE2(name, param) ? AI_VALUE2(type, name, param) : emptyval)

#define SET_AI_VALUE(type, name, value) context->GetValue<type>(name)->Set(value)
#define SET_AI_VALUE2(type, name, param, value) context->GetValue<type>(name, param)->Set(value)
#define RESET_AI_VALUE(type, name) context->GetValue<type>(name)->Reset()
#define RESET_AI_VALUE2(type, name, param) context->GetValue<type>(name, param)->Reset()

#define PAI_VALUE(type, name) PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->GetValue<type>(name)->Get()
#define PAI_VALUE2(type, name, param) PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->GetValue<type>(name, param)->Get()
#define SET_PAI_VALUE(type, name, value) PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->GetValue<type>(name)->Set(value)
#define SET_PAI_VALUE2(type, name, param, value) PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->GetValue<type>(name, param)->Set(value)
#define PHAS_AI_VALUE(name) PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->HasValue(name)
#define PHAS_AI_VALUE2(name, param) PlayerbotAIStorage::Instance().GetAI(player)->GetAiObjectContext()->HasValue(name, param)
#define MAI_VALUE(type, name) PlayerbotAIStorage::Instance().GetAI(master)->GetAiObjectContext()->GetValue<type>(name)->Get()
#define MAI_VALUE2(type, name, param) PlayerbotAIStorage::Instance().GetAI(master)->GetAiObjectContext()->GetValue<type>(name, param)->Get()

#define GAI_VALUE(type, name) sSharedObjectContext.GetValue<type>(name)->Get()
#define GAI_VALUE2(type, name, param) sSharedObjectContext.GetValue<type>(name, param)->Get()
#define SET_GAI_VALUE(type, name, value) sSharedObjectContext.GetValue<type>(name)->Set(value)
#define SET_GAI_VALUE2(type, name, param, value) sSharedObjectContext.GetValue<type>(name, param)->Set(value)

#define MEM_AI_VALUE(type, name) dynamic_cast<MemoryCalculatedValue<type>*>(context->GetUntypedValue(name))
#define LOG_AI_VALUE(type, name) dynamic_cast<LogCalculatedValue<type>*>(context->GetUntypedValue(name))
