#include "pch.h"

#include <cstring>
#include <iostream>
#include <vector>

#include "common.h"
#include "AnimationBinder.h"
#include "HKXWriter.h"
#include "SkeletonLoader.h"
#include "XMLReader.h"

void output(int argc, char** argv)
{
	//args
	//1. input xml
	//2. output file name
	//3+. skeleton(s)
	if (argc >= 3) {
		iohkx::HavokEngine engine;

		std::cout << "---SkeletonLoader starting---\n";
		iohkx::SkeletonLoader skeleton(engine);
		skeleton.m_options.loadHierarchies = false;
		skeleton.load(argc - 2, argv + 2);
		if (skeleton.empty())
			throw iohkx::Exception(iohkx::ERR_INVALID_INPUT, "No skeleton found");
		std::cout << "---SkeletonLoader finished---\n";

		std::cout << "---XMLReader starting---\n";
		iohkx::XMLReader xml;
		xml.read(argv[0], skeleton[0]);
		std::cout << "---XMLReader finished---\n";

		std::cout << "---AnimationBinder starting---\n";
		iohkx::AnimationBinder animation(engine);
		animation.bind(xml.m_data, skeleton[0]);
		std::cout << "---AnimationBinder finished---\n";

		std::cout << "---HKXWriter starting---\n";
		iohkx::HKXWriter hkx(engine);
		hkx.m_options.textFormat = true;
		hkx.write(animation.m_binding, argv[1]);
		std::cout << "---HKXWriter finished---\n";
	}
	else
		throw iohkx::Exception(iohkx::ERR_INVALID_ARGS, "Missing arguments");
}

int _tmain(int argc, _TCHAR** argv)
{
	try {
		if (argc > 1) {
			if (std::strcmp(argv[1], "out") == 0)
				output(argc - 2, argv + 2);
			else
				throw iohkx::Exception(iohkx::ERR_INVALID_ARGS, "Invalid command");
		}
		else {
			//show help?
			throw iohkx::Exception(iohkx::ERR_INVALID_ARGS, "No arguments");
		}
	}
	catch (const iohkx::Exception& e) {
		std::cerr << e.msg << std::endl;
		return e.code;
	}
	return 0;
}
