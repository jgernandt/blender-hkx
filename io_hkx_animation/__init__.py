import io_hkx_animation.hkx_export
import io_hkx_animation.properties

bl_info = {
    'name': "HKX Animation",
    'author': "Jonas Gernandt",
    'version': (0, 1, 0),
    'blender': (3, 1, 0),
    'location': "File > Export",
    'description': "",
    'doc_url': "",
    'category': "Import-Export"}

def register():
    hkx_export.register()
    properties.register()

def unregister():
    hkx_export.unregister()
    properties.unregister()
