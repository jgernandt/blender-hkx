#include "pch.h"
#include "HavokEngine.h"

#include "Common/Base/Memory/System/Util/hkMemoryInitUtil.h"
#include "Common/Base/Memory/Allocator/Malloc/hkMallocAllocator.h"

static void HK_CALL errorReport(const char* msg, void* userArgGivenToInit)
{
	std::cerr << msg << '\n';
}

iohkx::HavokEngine::HavokEngine()
{
	hkMemoryRouter* memoryRouter = hkMemoryInitUtil::initDefault(
		hkMallocAllocator::m_defaultMallocAllocator, hkMemorySystem::FrameInfo(0));
	hkBaseSystem::init( memoryRouter, errorReport );
}

iohkx::HavokEngine::~HavokEngine()
{
	hkBaseSystem::quit();
	hkMemoryInitUtil::quit();
}
