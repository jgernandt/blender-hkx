import bpy


AXES = [
    ("X", "X", "X axis"), 
    ("Y", "Y", "Y axis"), 
    ("Z", "Z", "Z axis"),
    ("-X", "-X", "Negative X axis"), 
    ("-Y", "-Y", "Negative Y axis"), 
    ("-Z", "-Z", "Negative Z axis"),
]


class ArmaturePanel(bpy.types.Panel):
    """Panel for the Armature properties window"""
    bl_label = "HKX Export"
    bl_idname = "DATA_PT_iohkx_armature"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "data"
    
    @classmethod
    def poll(cls, context):
        return (context.object and context.object.type == 'ARMATURE')
    
    def draw(self, context):
        self.layout.prop(context.object.data.iohkx, "skeleton_path")
        
        #Is it stupid to show these here? 
        #Makes no sense to change them outside the import/export dialog.
        self.layout.prop(context.object.data.iohkx, "bone_forward")
        self.layout.prop(context.object.data.iohkx, "bone_up")


class ArmatureProperties(bpy.types.PropertyGroup):
    skeleton_path: bpy.props.StringProperty(
        name="Skeleton",
        description="Path to the HKX skeleton file",
        subtype='FILE_PATH')
    
    bone_forward: bpy.props.EnumProperty(
        items=AXES,
        name="Forward axis",
        description="This axis will be mapped to Blender's Y axis",
        default='Y',
        #update=callbackfcn
    )
    
    bone_up: bpy.props.EnumProperty(
        items=AXES,
        name="Up axis",
        description="This axis will be mapped to Blender's Z axis",
        default='Z',
        #update=callbackfcn
    )


class BonePanel(bpy.types.Panel):
    """Panel for the Bone properties window"""
    bl_label = "HKX Export"
    bl_idname = "DATA_PT_iohkx_bone"
    bl_space_type = 'PROPERTIES'
    bl_region_type = 'WINDOW'
    bl_context = "bone"
    
    @classmethod
    def poll(cls, context):
        #there's no convenient way to access our BoneProperties from an EditBone?
        return context.active_bone and context.mode != 'EDIT_ARMATURE'
        
    def draw(self, context):
        self.layout.prop(context.active_bone.iohkx, "hkx_name")


class BoneProperties(bpy.types.PropertyGroup):
    hkx_name: bpy.props.StringProperty(
        name="HKX name",
        description="The name of this bone in imported/exported HKX files"
    )


def register():
    bpy.utils.register_class(ArmatureProperties)
    bpy.utils.register_class(ArmaturePanel)
    bpy.types.Armature.iohkx = bpy.props.PointerProperty(type=ArmatureProperties)
    
    bpy.utils.register_class(BoneProperties)
    bpy.utils.register_class(BonePanel)
    bpy.types.Bone.iohkx = bpy.props.PointerProperty(type=BoneProperties)


def unregister():
    del bpy.types.Armature.iohkx
    bpy.utils.unregister_class(ArmaturePanel)
    bpy.utils.unregister_class(ArmatureProperties)
    
    del bpy.types.Bone.iohkx
    bpy.utils.unregister_class(BonePanel)
    bpy.utils.unregister_class(BoneProperties)
