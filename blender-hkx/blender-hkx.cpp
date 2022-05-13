#include "pch.h"

#include "common.h"
#include "AnimationDecoder.h"
#include "HKXInterface.h"
#include "SkeletonLoader.h"
#include "XMLInterface.h"

using namespace iohkx;

constexpr const char* VERSION_STR = "0.1.0";

void about()
{
	std::cout << '\n';
	std::cout << "Blender-HKX version " << VERSION_STR << '\n';
	std::cout << "Copyright 2022 Jonas Gernandt.\n\n";

	std::cout << "Blender-HKX is a tool for converting Havok animation files to a readable format.\n\
It is part of a Blender addon and is only intended for use with Blender.\n\n";

	std::cout << "Blender-HKX contains proprietary subprograms and may not be redistributed.\n\n";

	std::cout << "Blender-HKX uses Havok (https://www.havok.com). \n\
Havok software is Copyright 2020 Microsoft. All rights reserved.\n\n";

	std::cout << "Blender-HKX uses pugixml (https://pugixml.org).\n\
pugixml is Copyright 2006-2019 Arseny Kapoulkine.\n";
}

void unpack(int argc, char* const* argv)
{
	//args
	//1. hkx file name
	//2. output xml
	//3+. skeleton(s)
	if (argc >= 3) {
		HavokEngine engine;
		HKXInterface hkx;

		SkeletonLoader skeletons;

		for (int i = 0; i < argc - 2; i++) {
			hkRefPtr<hkaAnimationContainer> res = hkx.load(argv[i + 2]);
			if (res)
				skeletons.load(res.val());
		}
		if (skeletons.empty())
			throw Exception(ERR_INVALID_INPUT, "No skeleton found");

		hkRefPtr<hkaAnimationContainer> anim = hkx.load(argv[0]);

		AnimationDecoder animation;
		animation.decompress(anim, skeletons.get());

		XMLInterface xml;
		xml.write(animation.get(), argv[1]);
	}
	else
		throw Exception(ERR_INVALID_ARGS, "Missing arguments");
}

void pack(int argc, char* const* argv)
{
	//args
	//1. format specifier
	//2. input xml
	//3. output file name
	//4+. skeleton(s)
	if (argc >= 4) {
		HavokEngine engine;
		HKXInterface hkx;

		SkeletonLoader skeleton;

		for (int i = 0; i < argc - 3; i++) {
			hkRefPtr<hkaAnimationContainer> res = hkx.load(argv[i + 3]);
			if (res)
				skeleton.load(res.val());
		}
		if (skeleton.empty())
			throw Exception(ERR_INVALID_INPUT, "No skeleton found");

		AnimationDecoder animation;

		XMLInterface xml;
		xml.read(argv[1], skeleton.get(), animation.get());

		hkRefPtr<hkaAnimationContainer> anim = animation.compress();

		if (_stricmp(argv[0], "WIN32") == 0) {
			hkx.m_options.layout = LAYOUT_WIN32;
		}
		else if (_stricmp(argv[0], "XML") == 0) {
			hkx.m_options.textFormat = true;
		}
		else {
			hkx.m_options.layout = LAYOUT_AMD64;
		}
		hkx.save(anim.val(), argv[2]);
	}
	else
		throw Exception(ERR_INVALID_ARGS, "Missing arguments");
}

int _tmain(int argc, _TCHAR** argv)
{
	try {
		if (argc > 1) {
			if (std::strcmp(argv[1], "unpack") == 0)
				unpack(argc - 2, argv + 2);
			else if (std::strcmp(argv[1], "pack") == 0)
				pack(argc - 2, argv + 2);
			else
				about();
		}
		else {
			about();
		}
	}
	catch (const Exception& e) {
		std::cerr << e.msg << std::endl;
		return e.code;
	}
	return 0;
}

#if _MSC_VER >= 1900

//__iob_func is called from Havok, but it no longer exists.
//The FILE struct that it returns has also changed, so it
//won't even make sense to try to find one and return it.
// (https://stackoverflow.com/a/34655235 for details)
// 
//We just provide a dummy definition to satisfy the linker and let the program
//crash and burn if it ever gets called (fingers crossed).

#ifdef __cplusplus
extern "C"
#endif
FILE* __cdecl __iob_func(unsigned i) 
{
	assert(false);
	return nullptr;
}

#endif
