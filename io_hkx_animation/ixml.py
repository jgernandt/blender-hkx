import xml.dom.minidom as xml

import mathutils

#global _implementation?

def unpacktransform(string):
    floats = [float(word) for word in string.split()]
    assert len(floats) == 10, "invalid transform data"
    
    loc = mathutils.Vector(floats[0:3])
    #Havok has scalar last!
    rot = mathutils.Quaternion([floats[6], floats[3], floats[4], floats[5]])
    scl = mathutils.Vector(floats[7:10])
    
    return loc, rot, scl

class AnimationInterface():
    name: str
    
    def __init__(self, node):
        self._node = node

class SkeletonInterface():
    name: str
    
    def __init__(self, node):
        self._node = node
        self.name = node.getAttribute("name")
    
    def bones(self):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == "bone":
                yield SkeletonInterface(node)
    
    def refpose(self):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == "transform":
                assert len(node.childNodes) == 1, "unexpected text"
                assert node.firstChild.nodeType == node.TEXT_NODE, "unexpected node"
                return unpacktransform(node.firstChild.data)
        
        #Transform must not be missing
        raise RuntimeError("Bone missing reference pose")


class DocumentInterface():
    def __init__(self, file):
        self._dom = xml.parse(file)
        self._root = self._dom.documentElement
        assert self._root.tagName == "blender-hkx", "invalid file"
        assert self._root.getAttribute("version") == "1", "unsupported version"
    
    def __del__(self):
        #probably not necessary
        self._dom.unlink()
    
    def animations(self):
        for node in self._root.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == "animation":
                yield AnimationInterface(node)
    
    def skeletons(self):
        for node in self._root.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == "skeleton":
                yield SkeletonInterface(node)

