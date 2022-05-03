#pragma once

#include <cassert>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <tchar.h>

#include "Common/Base/hkBase.h"
#include <Common/Base/System/Io/IStream/hkIStream.h>

#include "Animation/Animation/hkaAnimationContainer.h"
#include "Animation/Animation/Animation/hkaAnimation.h"
#include "Animation/Animation/Rig/hkaSkeletonUtils.h"
#include "Animation/Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h"

#include "Common/Serialize/Packfile/hkPackfileWriter.h"
#include "Common/Serialize/Util/hkRootLevelContainer.h"
#include "Common/Serialize/Util/hkSerializeUtil.h"

#include "pugixml.hpp"
