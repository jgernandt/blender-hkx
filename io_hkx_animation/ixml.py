from enum import Enum
import xml.dom.minidom as xml

import mathutils

TAG_ANIMATION = "animation"
TAG_ANNOTATION = "annotation"
TAG_BONE = "bone"
TAG_BOOL = "bool"
TAG_FLOAT = "float"
TAG_FLOATSLOT = "slot"
TAG_INT = "int"
TAG_SKELETON = "skeleton"
TAG_STRING = "string"
TAG_TRACK = "track"
TAG_TRANSFORM = "transform"

ATTR_ADDITIVE = "additive"
ATTR_FRAME = "frame"
ATTR_FRAMES = "frames"
ATTR_FRAMERATE = "frameRate"
ATTR_REFERENCE = "ref"
ATTR_REFERENCE_FRAME = "referenceFrame"
ATTR_SKELETON = "skeleton"
ATTR_TEXT = "text"

CURRENT_VERSION = "1"
SUPPORTED_VERSIONS = {"1"}

class ReferenceFrame(Enum):
    UNDEFINED = ""
    OBJECT = "OBJECT"
    BONE = "BONE"
    PARENT = "PARENT_BONE"

class Track(Enum):
    FLOAT = TAG_FLOAT
    TRANSFORM = TAG_TRANSFORM

def unpack_transform(string):
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
    
    def add_element(self, tagname, attributes):
        e = self.doc.createElement(tagname)
        self.node.appendChild(e)
        
        for name, value in attributes.items():
            e.setAttribute(name, value)
        
        return e
    
    def get_elements(self, tagName=""):
        for node in self.node.childNodes:
            if node.nodeType == node.ELEMENT_NODE and (tagName == "" or node.tagName == tagName):
                yield node
    
    def get_string_data(self, name=""):
        for child in self.get_elements(tagName=TAG_STRING):
            if child.getAttribute("name") == name:
                #we assume the text node already exists. stupid?
                return child.firstChild.data
                
        #else return None? empty string? create the element?
        return ""
    
    def set_string_data(self, name="", value=""):
        #find string element with name == name, or create if missing
        for child in self.get_elements(tagName=TAG_STRING):
            if child.getAttribute("name") == name:
                #we assume the text node already exists. stupid?
                child.firstChild.data = value
                return
        
        #missing element, create it
        e = self.doc.createElement(TAG_STRING)
        e.setAttribute("name", name)
        e.appendChild(self.doc.createTextNode(value))
        self.node.appendChild(e)


class AnnotationInterface(DOMInterface):
    frame: int
    text: str
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        #find frame element
        e = None
        for child in self.get_elements(TAG_INT):
            if child.getAttribute("name") == ATTR_FRAME:
                e = child
        
        #create elements if missing
        if not e:
            e = self.add_element(TAG_INT, {"name" : ATTR_FRAME})
            e.appendChild(self.doc.createTextNode("-1"))
        
        #start counting frames at 1
        self.frame = int(e.firstChild.data) + 1
        
        #find text element
        e = None
        for child in self.get_elements(TAG_STRING):
            if child.getAttribute("name") == ATTR_TEXT:
                e = child
        
        #create if missing
        if not e:
            e = self.add_element(TAG_STRING, {"name" : ATTR_TEXT})
            e.appendChild(self.doc.createTextNode(""))
        
        self.text = e.firstChild.data
    
    def set_frame(self, frame):
        self.frame = frame
        for child in self.get_elements(TAG_INT):
            if child.getAttribute("name") == ATTR_FRAME:
                #output frame number counting from 0
                child.firstChild.data = str(frame - 1)
    
    def set_text(self, text):
        self.text = text
        for child in self.get_elements(TAG_STRING):
            if child.getAttribute("name") == ATTR_TEXT:
                child.firstChild.data = text

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
        self.value = unpack_transform(node.firstChild.data)
    
    def set_value(self, loc, rot, scl):
        self.value = loc, rot, scl
        self.node.firstChild.data = pack_transform(loc, rot, scl)


class FloatKeyInterface(KeyInterface):
    value: float
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        if node.firstChild.data == "":
            node.firstChild.data = "0.0"
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
        e = self.add_element(self.datatype.value, {"name" : str(index)})
        return self.keytype(self.doc, e)
    
    def keys(self):
        for node in self.get_elements(tagName=self.datatype.value):
            yield self.keytype(self.doc, node)


class FloatTrackInterface(TrackInterface):
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.datatype = Track.FLOAT
        self.keytype = FloatKeyInterface


class TransformTrackInterface(TrackInterface):
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.datatype = Track.TRANSFORM
        self.keytype = TransformKeyInterface


class AnimationInterface(DOMInterface):
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
    
    def add_annotation(self, frame, text):
        e = self.add_element(TAG_ANNOTATION, {})
        ianno = AnnotationInterface(self.doc, e)
        ianno.set_frame(frame)
        ianno.set_text(text)
        return ianno
    
    def add_float_track(self, name):
        e = self.add_element(TAG_TRACK, {"name" : name, "type" : Track.FLOAT.value})
        return FloatTrackInterface(self.doc, e)
    
    def add_transform_track(self, name):
        e = self.add_element(TAG_TRACK, {"name" : name, "type" : Track.TRANSFORM.value})
        return TransformTrackInterface(self.doc, e)
    
    def set_reference_frame(self, ref):
        #ref should be a ReferenceFrame
        self.set_string_data(name=ATTR_REFERENCE_FRAME, value=ref.value)
    
    def set_skeleton_name(self, name):
        self.set_string_data(name=ATTR_SKELETON, value=name)
    
    def annotations(self):
        for node in self.get_elements(tagName=TAG_ANNOTATION):
            yield AnnotationInterface(self.doc, node)
    
    def tracks(self):
        for node in self.get_elements(tagName=TAG_TRACK):
            datatype = node.getAttribute("type")
            if datatype == Track.FLOAT.value:
                yield FloatTrackInterface(self.doc, node)
            elif datatype == Track.TRANSFORM.value:
                yield TransformTrackInterface(self.doc, node)


class SkeletonInterface(DOMInterface):
    name: str
    #reference: undefined
    
    def __init__(self, doc, node):
        super().__init__(doc, node)
        
        self.name = node.getAttribute("name")
        
        isbone = node.tagName == TAG_BONE
        
        if isbone:
            tag = TAG_TRANSFORM
        else:
            tag = TAG_FLOAT
        
        #find reference element
        e = None
        for child in self.get_elements(tag):
            if child.getAttribute("name") == ATTR_REFERENCE:
                e = child
        
        if e:
            if isbone:
                self.reference = unpack_transform(e.firstChild.data)
            else:
                self.reference = float(e.firstChild.data)
        #create if missing
        else:
            e = self.add_element(tag, {"name" : ATTR_REFERENCE})
            e.appendChild(self.doc.createTextNode(""))
            self.reference = None
    
    def bones(self):
        for node in self.get_elements(tagName=TAG_BONE):
            yield SkeletonInterface(self.doc, node)
    
    def floats(self):
        for node in self.get_elements(tagName=TAG_FLOATSLOT):
            yield SkeletonInterface(self.doc, node)


class DocumentInterface(DOMInterface):
    animations: [AnimationInterface]
    skeletons: [SkeletonInterface]
    
    def __init__(self, doc):
        super().__init__(doc, doc.documentElement)
        
        assert self.node.tagName == "blender-hkx", "invalid file"
        assert self.node.getAttribute("version") in SUPPORTED_VERSIONS, "unsupported version"
        
        self.animations = []
        for node in self.get_elements(tagName=TAG_ANIMATION):
            self.animations.append(AnimationInterface(doc, node))
                
        self.skeletons = []
        for node in self.get_elements(tagName=TAG_SKELETON):
            self.skeletons.append(SkeletonInterface(doc, node))
    
    def __del__(self):
        #probably not necessary
        self.doc.unlink()
    
    def save(self, filename):
        with open(filename, mode='w', encoding="UTF-8") as file:
            self.doc.writexml(file, addindent='\t', newl='\n', encoding="UTF-8")#debug mode
            #self.doc.writexml(file, encoding="UTF-8")
    
    def add_animation(self, name):
        e = self.add_element(TAG_ANIMATION, {"name" : name})
        ianim = AnimationInterface(self.doc, e)
        if ianim:
            self.animations.append(ianim)
        return ianim
    
    def set_additive(self, value):
        self._set_param(TAG_BOOL, ATTR_ADDITIVE, value)
    
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
