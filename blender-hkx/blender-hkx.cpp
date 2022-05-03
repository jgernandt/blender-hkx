#include "pch.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "common.h"
#include "AnimationDecoder.h"
#include "HKXInterface.h"
#include "SkeletonLoader.h"
#include "XMLInterface.h"

using namespace iohkx;

void about()
{
	//TODO
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

		std::cout << "---SkeletonLoader starting---\n";
		SkeletonLoader skeleton;
		skeleton.m_options.loadHierarchies = true;

		for (int i = 0; i < argc - 2; i++) {
			hkRefPtr<hkaAnimationContainer> res = hkx.load(argv[i + 2]);
			if (res)
				skeleton.load(res.val());
		}
		if (skeleton.empty())
			throw Exception(ERR_INVALID_INPUT, "No skeleton found");
		std::cout << "---SkeletonLoader finished---\n";

		hkRefPtr<hkaAnimationContainer> anim = hkx.load(argv[0]);
		AnimationData data;

		std::cout << "---AnimationDecoder starting---\n";
		AnimationDecoder decoder;
		decoder.decompress(anim, skeleton[0], data);
		std::cout << "---AnimationDecoder finished---\n";

		std::cout << "---XMLInterface starting---\n";
		XMLInterface xml;
		xml.write(data, argv[1]);
		std::cout << "---XMLInterface finished---\n";
	}
	else
		throw Exception(ERR_INVALID_ARGS, "Missing arguments");
}

void pack(int argc, char* const* argv)
{
	//args
	//1. input xml
	//2. output file name
	//3+. skeleton(s)
	if (argc >= 3) {
		HavokEngine engine;
		HKXInterface hkx;

		std::cout << "---SkeletonLoader starting---\n";
		SkeletonLoader skeleton;
		skeleton.m_options.loadHierarchies = false;

		for (int i = 0; i < argc - 2; i++) {
			hkRefPtr<hkaAnimationContainer> res = hkx.load(argv[i + 2]);
			if (res)
				skeleton.load(res.val());
		}
		if (skeleton.empty())
			throw Exception(ERR_INVALID_INPUT, "No skeleton found");
		std::cout << "---SkeletonLoader finished---\n";

		AnimationData data;

		std::cout << "---XMLInterface starting---\n";
		XMLInterface xml;
		xml.read(argv[0], skeleton[0], data);
		std::cout << "---XMLInterface finished---\n";

		std::cout << "---AnimationDecoder starting---\n";
		AnimationDecoder decoder;
		hkRefPtr<hkaAnimationContainer> anim = decoder.compress(data, skeleton[0]);
		std::cout << "---AnimationDecoder finished---\n";

		hkx.m_options.textFormat = true;
		hkx.save(anim.val(), argv[1]);
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
