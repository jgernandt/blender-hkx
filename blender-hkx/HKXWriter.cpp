#include "pch.h"
#include "HKXWriter.h"

iohkx::HKXWriter::HKXWriter(const HavokEngine& engine)
{
	m_options.textFormat = false;
	m_options.layout = LAYOUT_AMD64;
}

void iohkx::HKXWriter::write(hkRefPtr<hkaAnimationBinding> binding, const std::string& file)
{
	hkRootLevelContainer root;

	hkRefPtr<hkaAnimationContainer> animCtnr = new hkaAnimationContainer;
	animCtnr->removeReference();

	root.m_namedVariants.pushBack(hkRootLevelContainer::NamedVariant(
			"Merged Animation Container", animCtnr.val(), &animCtnr->staticClass()));

	animCtnr->m_bindings.pushBack(binding.val());
	
	assert(binding->m_animation);
	animCtnr->m_animations.pushBack(binding->m_animation);

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
		hkOstream(file.c_str()).getStreamWriter(), pfopts, HK_NULL, opts);

	if (res == HK_FAILURE)
		throw Exception(ERR_WRITE_FAIL, "Failed to save file");
}
