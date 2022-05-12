from enum import Enum
import xml.dom.minidom as xml

import mathutils

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
ATTR_ADDITIVE = "additive"
ATTR_SKELETON = "skeleton"

CURRENT_VERSION = "1"
SUPPORTED_VERSIONS = {"1"}

class Track(Enum):
    FLOAT = TAG_FLOAT
    TRANSFORM = TAG_TRANSFORM

def unpacktransform(string):
    floats = [float(word) for word in string.split()]
    
    if len(floats) == 10:
        loc = mathutils.Vector(floats[0:3])
        rot = mathutils.Quaternion(floats[3:7])
        scl = mathutils.Vector(floats[7:10])
    else:
        loc = mathutils.Vector((0.0, 0.0, 0.0))
        rot = mathutils.Quaternion()
        scl = mathutils.Vector((1.0, 1.0, 1.0))
    
    return loc, rot, scl


def pack_transform(loc, rot, scl):
    return " ".join((str(loc[0]), str(loc[1]), str(loc[2]), 
            str(rot[0]), str(rot[1]), str(rot[2]), str(rot[3]), 
            str(scl[0]), str(scl[1]), str(scl[2])))


class DOMInterface():
    doc: xml.Document
    node: xml.Node
    
    def __init__(self, doc, node):
        self.doc = doc
        self.node = node
    
    def add_element(self, objtype, tagname, attributes):
        e = self.doc.createElement(tagname)
        self.node.appendChild(e)
        
        for name, value in attributes.items():
            e.setAttribute(name, value)
        
        return objtype(self.doc, e)
        


class KeyInterface(DOMInterface):
    frame: int
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        #start counting frames at 1
        self.frame = int(node.getAttribute("name")) + 1
        
        if len(node.childNodes) == 0:
            node.appendChild(doc.createTextNode(""))
        
        assert len(node.childNodes) == 1, "unexpected text"
        assert node.firstChild.nodeType == node.TEXT_NODE, "unexpected node"


class TransformKeyInterface(KeyInterface):
    value: (mathutils.Vector, mathutils.Quaternion, mathutils.Vector)
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        self.value = unpacktransform(node.firstChild.data)
    
    def set_value(self, loc, rot, scl):
        self.value = loc, rot, scl
        self.node.firstChild.data = pack_transform(loc, rot, scl)


class FloatKeyInterface(KeyInterface):
    value: float
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        self.value = float(node.firstChild.data)
    
    def set_value(self, value):
        self.value = value
        self.node.firstChild.data = str(value)


class TrackInterface(DOMInterface):
    name: str
    datatype: Track
    #keytype: 
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.name = node.getAttribute("name")
    
    def add_key(self, index):
        ikey = self.add_element(self.keytype, self.datatype.value, {"name" : str(index)})
        return ikey
    
    def keys(self):
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == self.datatype.value:
                yield self.keytype(self.doc, node)


class FloatTrackInterface(TrackInterface):
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.datatype = Track.FLOAT
        self.keytype = FloatKeyInterface
    
    def add_key(self, index):
        return super().add_key(index)


class TransformTrackInterface(TrackInterface):
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.datatype = Track.TRANSFORM
        self.keytype = TransformKeyInterface
    
    def add_key(self, index):
        return super().add_key(index)


class AnimationInterface(DOMInterface):
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        #find skeleton name
        self._skeleton = None
        for child in node.childNodes:
            if child.nodeType == child.ELEMENT_NODE and child.tagName == TAG_STRING and child.getAttribute("name") == ATTR_SKELETON:
                self._skeleton = child
                break
    
    def add_float_track(self, name):
        return self.add_element(FloatTrackInterface, TAG_TRACK, {"name" : name, "type" : Track.FLOAT.value})
    
    def add_transform_track(self, name):
        return self.add_element(TransformTrackInterface, TAG_TRACK, {"name" : name, "type" : Track.TRANSFORM.value})
    
    def set_skeleton_name(self, name):
        if self._skeleton:
            #replace this element's child by a new text node
            value = self.doc.createTextNode(name)
            self._skeleton.replaceChild(self._skeleton.firstChild, value)
            pass
        else:
            #create new element and text node
            e = self.doc.createElement(TAG_STRING)
            e.setAttribute("name", ATTR_SKELETON)
            e.appendChild(self.doc.createTextNode(name))
            self.node.appendChild(e)
    
    def tracks(self):
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_TRACK:
                datatype = node.getAttribute("type")
                if datatype == Track.FLOAT.value:
                    yield FloatTrackInterface(self.doc, node)
                elif datatype == Track.TRANSFORM.value:
                    yield TransformTrackInterface(self.doc, node)


class SkeletonInterface(DOMInterface):
    name: str
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.name = node.getAttribute("name")
    
    def bones(self):
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_BONE:
                yield SkeletonInterface(self.doc, node)
    
    def refpose(self):
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_TRANSFORM:
                assert len(node.childNodes) == 1, "unexpected text"
                assert node.firstChild.nodeType == node.TEXT_NODE, "unexpected node"
                return unpacktransform(node.firstChild.data)
        
        #Transform must not be missing
        raise RuntimeError("Bone missing reference pose")


class DocumentInterface(DOMInterface):
    animations: [AnimationInterface]
    skeletons: [SkeletonInterface]
    
    def __init__(self, doc):
        super().__init__(doc, doc.documentElement)
        
        assert self.node.tagName == "blender-hkx", "invalid file"
        assert self.node.getAttribute("version") in SUPPORTED_VERSIONS, "unsupported version"
        
        self.animations = []
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_ANIMATION:
                self.animations.append(AnimationInterface(doc, node))
                
        self.skeletons = []
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == TAG_SKELETON:
                self.skeletons.append(SkeletonInterface(doc, node))
    
    def __del__(self):
        #probably not necessary
        self.doc.unlink()
    
    def save(self, filename):
        with open(filename, 'w') as file:
            self.doc.writexml(file, addindent='\t', newl='\n', encoding="UTF-8")
    
    def add_animation(self, name):
        ianim = self.add_element(AnimationInterface, TAG_ANIMATION, {"name" : name})
        if ianim:
            self.animations.append(ianim)
        return ianim
    
    def set_additive(self, value):
        self._set_param(TAG_STRING, ATTR_ADDITIVE, value)
    
    def set_frames(self, value):
        self._set_param(TAG_INT, ATTR_FRAMES, value)
    
    def set_framerate(self, value):
        self._set_param(TAG_INT, ATTR_FRAMERATE, value)
    
    def _set_param(self, tag, name, value):
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and node.tagName == tag:
                if node.getAttribute("name") == name:
                    #just assume the text node exists
                    node.firstChild.data = str(value)
                    return
        
        #it doesn't exist, create it
        e = self.doc.createElement(tag)
        e.setAttribute("name", name)
        e.appendChild(self.doc.createTextNode(str(value)))
        self.node.appendChild(e)
    
    def open(file):
        doc = xml.parse(file)
        return DocumentInterface(doc)
    
    def create():
        impl = xml.getDOMImplementation()
        doc = impl.createDocument(None, "blender-hkx", None)
        doc.documentElement.setAttribute("version", CURRENT_VERSION)
        return DocumentInterface(doc)
