import math
import os

import bpy
import bpy_extras
import mathutils

from io_hkx_animation.prefs import EXEC_NAME
from io_hkx_animation.ixml import DocumentInterface

import logging
log = logging.getLogger(__name__)
log.setLevel(0)

SAMPLING_RATE = 30

class HKXIO(bpy.types.Operator):
    
    length_scale: bpy.props.FloatProperty(
        name="Length scale",
        description="Scale factor for length units",
        default=10.0)
    
    #axis mapping

class HKXImport(HKXIO, bpy_extras.io_utils.ImportHelper):
    bl_label = 'Import'
    bl_idname = 'io_hkx_animation.import'
    bl_description = "Import HKX animation"
    bl_options = {'UNDO'}
    
    filename_ext = ".hkx"
    
    filter_glob: bpy.props.StringProperty(
        default="*.hkx",
        options={'HIDDEN'})
    
    skeleton_path: bpy.props.StringProperty(
        name="Skeleton",
        description="Path to the HKX skeleton file",
    )
    
    def invoke(self, context, event):
        #set skeleton_path to the path of the active armature (default if none)
        active_obj = context.view_layer.objects.active
        if active_obj and active_obj.type == 'ARMATURE':
            self.skeleton_path = active_obj.data.iohkx.skeleton_path
        if self.skeleton_path == "":
            self.skeleton_path = context.preferences.addons[__package__].preferences.default_skeleton
        
        #forward to ImportHelper
        return bpy_extras.io_utils.ImportHelper.invoke(self, context, event)
    
    def execute(self, context):
        try:
            #Look for the converter
            pref = context.preferences.addons[__package__].preferences.converter_tool
            exe = os.path.join(os.path.dirname(pref), EXEC_NAME)
            if not os.path.exists(exe):
                raise RuntimeError("Converter tool not found. Check your Addon Preferences.")
            
            #Invoke the converter
            
            #Load the xml
            doc = DocumentInterface(_tmpfilename(self.filepath))
            
            #If nothing is selected or active object is not an Armature, create a new one
            selected = context.view_layer.objects.selected
            active_obj = context.view_layer.objects.active
            #In case setting active fails (see below)
            armature = None
            if len(selected) == 0 or not active_obj or active_obj.type != 'ARMATURE':
                #switch to object mode (not strictly required?)
                if bpy.ops.object.mode_set.poll():
                    bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
                
                #deselect all
                for obj in selected:
                    obj.select_set(False)
                    
                #create new armatures
                armatures = [self.importskeleton(i, context) for i in doc.skeletons()]
                if len(armatures) == 0:
                    raise RuntimeError("File contains no skeletons")
                
                #select and make active
                armature = armatures[0]
                armature.select_set(True)
                #If previously active object is excluded from the view layer, setting active fails.
                #No idea why. Fringe case, though. Move on.
                context.view_layer.objects.active = armature
                
            else:
                armature = active_obj
            
            #add animation data if missing
            if not armature.animation_data:
                armature.animation_data_create()
            #create a new Action and set it as current
            d, name = os.path.split(self.filepath)
            root, ext = os.path.splitext(name)
            armature.animation_data.action = bpy.data.actions.new(name=root)
            
            #import keyframes
            
        except Exception as e:
            self.report({'ERROR'}, str(e))
        finally:
            #delete tmp file
            pass
        
        return {'FINISHED'}
    
    def importbone(self, ibone, parent, armature):
        log.debug("Importing bone " + ibone.name)
        #add bone to armature
        bone = armature.data.edit_bones.new(ibone.name)
        bone.length = 1.0
        if parent:
            bone.parent = parent
            bone.matrix = parent.matrix
        else:
            bone.matrix = mathutils.Matrix.Identity(4)
        
        #transform
        loc, rot, scl = ibone.refpose()
        loc /= self.length_scale
        bone.matrix = bone.matrix @ mathutils.Matrix.LocRotScale(loc, rot, scl)
        
        #recurse
        for ichild in ibone.bones():
            self.importbone(ichild, bone, armature)
    
    def importskeleton(self, iskeleton, context):
        log.debug("Importing skeleton " + iskeleton.name)
        #create armature object
        data = bpy.data.armatures.new(name=iskeleton.name)
        armature = bpy.data.objects.new(iskeleton.name, data)
        data.iohkx.skeleton_path = self.skeleton_path
        context.scene.collection.objects.link(armature)
        
        #start editing armature
        context.view_layer.objects.active = armature
        bpy.ops.object.mode_set(mode='EDIT', toggle=False)
        
        #add bones
        for ibone in iskeleton.bones():
            self.importbone(ibone, None, armature)
        
        #end edit
        bpy.ops.object.mode_set(mode='OBJECT', toggle=False)
        
        return armature




def _tmpfilename(file_name):
    root, ext = os.path.splitext(file_name)
    return root + "-tmp.xml"
    #return root + "-163498.xml"

_boneNameMap = dict({"WEAPON": "Weapon", "SHIELD": "Shield", "QUIVER": "Quiver"})


def _mapname(name):
    #Some names differ between nifs and hkxs. 
    #Specifically Weapon, Shield and Quiver, which are all caps in nif.
    #We want to output the hkx name.
    
    #This could be made configurable, similar to what niftools do.
    
    if name in _boneNameMap:
        return _boneNameMap[name]
    else:
        return name


class HKXExport(HKXIO, bpy_extras.io_utils.ExportHelper):
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

    def execute(self, context):
        try:
            #Check that we can find the exe
            
            
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
            skeleton_file = bpy.path.abspath(active_obj.data.iohkx.skeleton_path)
            if not os.path.exists(skeleton_file):
                raise RuntimeError("Skeleton file %s not found" % skeleton_file)
            
            #write tmp file
            tmp_file = _tmpfilename(self.filepath)
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
    

def exportop(self, context):
    self.layout.operator(HKXExport.bl_idname, text="Havok Animation (.hkx)")
    
def importop(self, context):
    self.layout.operator(HKXImport.bl_idname, text="Havok Animation (.hkx)")

def register():
    bpy.utils.register_class(HKXImport)
    bpy.utils.register_class(HKXExport)
    bpy.types.TOPBAR_MT_file_import.append(importop)
    bpy.types.TOPBAR_MT_file_export.append(exportop)

def unregister():
    bpy.types.TOPBAR_MT_file_export.remove(exportop)
    bpy.types.TOPBAR_MT_file_import.remove(importop)
    bpy.utils.unregister_class(HKXExport)
    bpy.utils.unregister_class(HKXImport)

