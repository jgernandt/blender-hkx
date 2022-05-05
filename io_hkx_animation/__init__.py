
import io_hkx_animation.prefs
import io_hkx_animation.props
import io_hkx_animation.ops

bl_info = {
    'name': "HKX Animation",
    'author': "Jonas Gernandt",
    'version': (0, 1, 0),
    'blender': (3, 1, 0),
    'location': "File > Import-Export",
    'description': "",
    'doc_url': "",
    'category': "Import-Export"}

def register():
    prefs.register()
    props.register()
    ops.register()

def unregister():
    ops.unregister()
    props.unregister()
    prefs.unregister()
