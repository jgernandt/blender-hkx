#include "pch.h"
#include "HKXInterface.h"

#ifdef _DEBUG
const char* FORMAT[] = {
	"FORMAT_ERROR",
		/// readable but not recognised.
	"FORMAT_UNKNOWN",
		/// binary packfile
	"FORMAT_PACKFILE_BINARY",
		/// XML packfile
	"FORMAT_PACKFILE_XML",
		/// Binary tagfile
	"FORMAT_TAGFILE_BINARY",
		/// XML tagfile
	"FORMAT_TAGFILE_XML",
};
#endif


iohkx::HKXInterface::HKXInterface()
{
}

hkRefPtr<hkaAnimationContainer> iohkx::HKXInterface::load(const char* fileName)
{
	hkRefPtr<hkaAnimationContainer> result;

	hkSerializeUtil::ErrorDetails err;
	hkIstream file(fileName);
	hkStreamReader* sr = file.getStreamReader();

#ifdef _DEBUG
	std::cout << "Loading " << fileName << "...\n";
#endif
	if (hkSerializeUtil::isLoadable(sr)) {

#ifdef _DEBUG
		hkSerializeUtil::FormatDetails format;
		hkSerializeUtil::detectFormat(sr, format);
		std::cout << "    Format: " << FORMAT[format.m_formatType] << '\n';
		std::cout << "    Version: " << format.m_version << '\n';
#endif

		hkRootLevelContainer* root = hkSerializeUtil::loadObject<hkRootLevelContainer>(sr, &err);
		if (err.id != hkSerializeUtil::ErrorDetails::ERRORID_NONE)
			throw Exception(ERR_READ_FAIL, err.defaultMessage);

		result = root->findObject<hkaAnimationContainer>();
		delete root;
	}
#ifdef _DEBUG
	else
		std::cout << "    File not loadable\n";
#endif

	return result;
}

void iohkx::HKXInterface::save(hkaAnimationContainer* animCtnr, const char* fileName)
{
	if (!animCtnr)
		return;

	hkRootLevelContainer root;

	root.m_namedVariants.pushBack(hkRootLevelContainer::NamedVariant(
			"Merged Animation Container", animCtnr, &animCtnr->staticClass()));

	hkPackfileWriter::Options pfopts;
	switch (m_options.layout) {
		case LAYOUT_WIN32:
			pfopts.m_layout = hkStructureLayout::MsvcWin32LayoutRules;
			break;
		case LAYOUT_AMD64:
			pfopts.m_layout = hkStructureLayout::MsvcAmd64LayoutRules;
			break;
	}

	hkSerializeUtil::SaveOptionBits bits = 
		m_options.textFormat ? hkSerializeUtil::SAVE_TEXT_FORMAT : hkSerializeUtil::SAVE_DEFAULT;
	hkSerializeUtil::SaveOptions opts(bits);

	hkResult res = hkSerializeUtil::savePackfile(&root, root.staticClass(), 
		hkOstream(fileName).getStreamWriter(), pfopts, HK_NULL, opts);

	if (res == HK_FAILURE)
		throw Exception(ERR_WRITE_FAIL, "Failed to save file");
}
