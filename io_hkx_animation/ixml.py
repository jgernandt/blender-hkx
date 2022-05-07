from enum import Enum
import xml.dom.minidom as xml

import mathutils

#global _implementation?

TAG_ANIMATION = "animation"
TAG_BONE = "bone"
TAG_FLOAT = "float"
TAG_INT = "int"
TAG_SKELETON = "skeleton"
TAG_STRING = "string"
TAG_TRACK = "track"
TAG_TRANSFORM = "transform"

ATTR_FRAMES = "frames"
ATTR_FRAMERATE = "frameRate"
ATTR_BLENDMODE = "blendMode"

VAL_ADDITIVE = "ADDITIVE"
VAL_NORMAL = "NORMAL"

class BlendMode(Enum):
    NORMAL = VAL_NORMAL
    ADDITIVE = VAL_ADDITIVE

class Track(Enum):
    FLOAT = TAG_FLOAT
    TRANSFORM = TAG_TRANSFORM

def unpacktransform(string):
    floats = [float(word) for word in string.split()]
    assert len(floats) == 10, "invalid transform data"
    
    loc = mathutils.Vector(floats[0:3])
    rot = mathutils.Quaternion(floats[3:7])
    scl = mathutils.Vector(floats[7:10])
    
    return loc, rot, scl


class TransformKeyInterface():
    frame: int
    value: (mathutils.Vector, mathutils.Quaternion, mathutils.Vector)
    
    def __init__(self, node):
        self._node = node
        #start counting frames at 1
        self.frame = int(node.getAttribute("name")) + 1
        
        assert len(node.childNodes) == 1, "unexpected text"
        assert node.firstChild.nodeType == node.TEXT_NODE, "unexpected node"
        
        self.value = unpacktransform(node.firstChild.data)


class FloatKeyInterface():
    frame: int
    value: float
    
    def __init__(self, node):
        self._node = node
        #start counting frames at 1
        self.frame = int(node.getAttribute("name")) + 1
        
        assert len(node.childNodes) == 1, "unexpected text"
        assert node.firstChild.nodeType == node.TEXT_NODE, "unexpected node"
        
        self.value = float(node.firstChild.data)


class TrackInterface():
    name: str
    datatype: Track
    
    def __init__(self, node):
        self._node = node
        self.name = node.getAttribute("name")
        self.datatype = Track(node.getAttribute("type"))
    
    def keys(self):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == self.datatype.value:
                if self.datatype == Track.FLOAT:
                    yield FloatKeyInterface(node)
                elif self.datatype == Track.TRANSFORM:
                    yield TransformKeyInterface(node)


class AnimationInterface():
    frames: int
    framerate: int
    blendmode: BlendMode
    
    def __init__(self, node):
        self._node = node
        parent = self._node.parentNode
        for node in parent.childNodes:
            if node.nodeType == node.ELEMENT_NODE: 
                if node.tagName == TAG_INT:
                    name = node.getAttribute("name")
                    if name == ATTR_FRAMES:
                        self.frames = int(node.firstChild.data)
                    elif name == ATTR_FRAMERATE:
                        self.framerate = int(node.firstChild.data)
                elif node.tagName == TAG_STRING:
                    if node.getAttribute("name") == ATTR_BLENDMODE:
                        self.blendMode = BlendMode(node.firstChild.data)
    
    def find(self, trackname):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_TRACK and node.getAttribute("name") == trackname:
                return TrackInterface(node)
    
    def tracks(self):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_TRACK:
                yield TrackInterface(node)


class SkeletonInterface():
    name: str
    
    def __init__(self, node):
        self._node = node
        self.name = node.getAttribute("name")
    
    def bones(self):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_BONE:
                yield SkeletonInterface(node)
    
    def refpose(self):
        for node in self._node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_TRANSFORM:
                assert len(node.childNodes) == 1, "unexpected text"
                assert node.firstChild.nodeType == node.TEXT_NODE, "unexpected node"
                return unpacktransform(node.firstChild.data)
        
        #Transform must not be missing
        raise RuntimeError("Bone missing reference pose")


class DocumentInterface():
    animations: AnimationInterface
    skeletons: SkeletonInterface
    
    def __init__(self, file):
        self._dom = xml.parse(file)
        self._root = self._dom.documentElement
        assert self._root.tagName == "blender-hkx", "invalid file"
        assert self._root.getAttribute("version") == "1", "unsupported version"
        
        self.animations = []
        for node in self._root.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_ANIMATION:
                self.animations.append(AnimationInterface(node))
                
        self.skeletons = []
        for node in self._root.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_SKELETON:
                self.skeletons.append(SkeletonInterface(node))
    
    def __del__(self):
        #probably not necessary
        self._dom.unlink()

