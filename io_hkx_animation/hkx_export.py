import math
import os
import random
import xml

import bpy
import bpy_extras
import mathutils

def gen_tmp_filename(file_name):
    root, ext = os.path.splitext(file_name)
    #id = random.getrandbits(32)
    #return "".join((root, '-', str(id), ".xml"))
    return root + "-tmp.xml"

_boneNameMap = dict({"WEAPON": "Weapon", "SHIELD": "Shield", "QUIVER": "Quiver"})

def map_bone_name(name):
    #Some names differ between nifs and hkxs. 
    #Specifically Weapon, Shield and Quiver, which are all caps in nif.
    #We want to output the hkx name.
    
    #This could be made configurable, similar to what niftools do.
    
    if name in _boneNameMap:
        return _boneNameMap[name]
    else:
        return name

#is Havok always 30 fps? Do we want this to be configurable?
SAMPLING_RATE = 30

class ActionGenerator(xml.sax.saxutils.XMLGenerator):
    def __init__(self, file_obj):
        xml.sax.saxutils.XMLGenerator.__init__(self, file_obj, encoding="UTF-8")
        self.indent = 0
    
    def startObject(self, name, attrs):
        self.ignorableWhitespace(self.indent * '\t')
        self.startElement(name, attrs)
        self.ignorableWhitespace("\n")
        self.indent += 1
    
    def endObject(self, name):
        self.indent -= 1
        self.ignorableWhitespace(self.indent * '\t')
        self.endElement(name)
        self.ignorableWhitespace('\n')
    
    def addData(self, name, value = None):
        self.ignorableWhitespace(self.indent * '\t')
        self.startElement(name, {})
        if value != None:
            self.characters(str(value))
        self.endElement(name)
        self.ignorableWhitespace("\n")
    
    def addTransformTrack(self, pose_bone):
        bone_name = map_bone_name(pose_bone.name)
        self.startObject("bone", {"name": bone_name})
        
        current_frame = self.scene.frame_current
        for i in range(self.frames):
            #set current frame (and subframe, if appropriate)
            if self.scene.render.fps == SAMPLING_RATE:
                self.scene.frame_set(self.start_frame + i)
            else:
                subframe, frame = math.modf(self.start_frame + i * self.frame_step)
                self.scene.frame_set(int(frame), subframe=abs(subframe))
                
            self.startObject("key", {})
            
            #convert bone transform to parent space (Havok style)
            #TODO: cache the bone-space transform, so we don't have to recalc this unless it changes
            if pose_bone.parent:
                try:
                    imat = pose_bone.parent.matrix.inverted()
                except:
                    raise RuntimeError("Scale must not be zero")
                loc, rot, scl = (imat @ pose_bone.matrix).decompose()
            else:
                loc, rot, scl = pose_bone.matrix.decompose()
            
            #convert to our space conventions
            loc *= self.lengthScale
            #TODO: axis transforms
            
            #output
            #self.addData("frame", str(i))
            self.addData("translation", "(%s, %s, %s)" % (loc[0], loc[1], loc[2]))
            #store quats with scalar component last (Havok style)
            self.addData("rotation", "(%s, %s, %s, %s)" % (rot[1], rot[2], rot[3], rot[0]))
            self.addData("scale", "(%s, %s, %s)" % (scl[0], scl[1], scl[2]))
            
            self.endObject("key")
        self.scene.frame_set(current_frame)
        
        self.endObject("bone")
    
    def generate(self, operator):
        #Note to self: should probably separate xml output from sampling logic
        
        self.frame_step = self.scene.render.fps / SAMPLING_RATE
        
        frame_steps = self.scene.frame_end - self.scene.frame_start
        if self.scene.render.fps != SAMPLING_RATE:
            operator.report({'WARNING'}, "Sampling animation at %s fps" % str(SAMPLING_RATE))
            frame_steps = int(round(frame_steps / self.frame_step))
        
        self.frames = frame_steps + 1
        self.start_frame = self.scene.frame_start
        
        self.startDocument()
        self.startObject("io_hkx_file", {"version": "1"})
        
        self.addData("nFrames", self.frames)
        self.addData("frameRate", SAMPLING_RATE)
        self.addData("blendMode", "NORMAL")
        
        self.startObject("clip", {"name": "0"})
        self.addData("skeleton", 0)
        
        for pose_bone in self.armature.pose.bones:
            #add some property to filter out mechanism bones?
            self.addTransformTrack(pose_bone)
        
        self.endObject("clip")
        self.endObject("io_hkx_file")
        self.endDocument()
    

class HKXExport(bpy.types.Operator, bpy_extras.io_utils.ExportHelper):
    bl_label = 'Export'
    bl_idname = 'io_hkx.export'
    bl_description = "Export animation as HKX"
    
    filename_ext = ".hkx"
    
    filter_glob: bpy.props.StringProperty(
        default="*.hkx",
        options={'HIDDEN'})
    
    hkx_blend_hint_additive: bpy.props.BoolProperty(
        name="Additive",
        description="Store offsets instead of pose",
        default=False)
    
    hkx_scale_factor: bpy.props.FloatProperty(
        name="Length scale",
        description="Scale factor for length units",
        default=10.0)

    def execute(self, context):
        
        try:
            #get the active object
            #   if none, throw
            selected = bpy.context.view_layer.objects.selected
            active_obj = bpy.context.view_layer.objects.active
            if len(selected) == 0 or not active_obj or active_obj.type != 'ARMATURE':
                raise RuntimeError("No active Armature")
                
            #get the active Action of the active object
            #   if none, throw
            #action = None
            #if active_obj.animation_data:
            #    action = active_obj.animation_data.action
            
            #if not action:
            #    raise RuntimeError("Active object has no active Action")
            
            #make sure that the skeleton file exists
            skeleton_file = bpy.path.abspath(active_obj.data.io_hkx.skeleton_file)
            if not os.path.exists(skeleton_file):
                raise RuntimeError("Skeleton file %s not found" % skeleton_file)
            
            #write tmp file
            tmp_file = gen_tmp_filename(self.filepath)
            with open(tmp_file, mode='w') as f:
                if self.hkx_blend_hint_additive:
                    gen = None
                else:
                    gen = ActionGenerator(f)
                gen.armature = active_obj
                gen.scene = context.scene
                gen.lengthScale = self.hkx_scale_factor
                gen.generate(self)
            
            #invoke exe
            
            self.report({'INFO'}, "Exported %s successfully" % self.filepath)
            
        except OSError as e:
            self.report({'ERROR'}, str(e))
        except RuntimeError as e:
            self.report({'ERROR'}, str(e))
        finally:
            #delete tmp file
            pass
        
        return {'FINISHED'}
    

def menu_operator(self, context):
    self.layout.operator(HKXExport.bl_idname, text="Havok Animation (.hkx)")

def register():
    bpy.utils.register_class(HKXExport)
    bpy.types.TOPBAR_MT_file_export.append(menu_operator)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(menu_operator)
    bpy.utils.unregister_class(HKXExport)

