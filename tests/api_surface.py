import re
from pathlib import Path

REGISTRIES = ['Items','Attributes','Skills','Status','Equipment','Interactions','Network','Permissions','Commands','WorldObjects','Activities','Organizations','Director','Movement','Animation']
ALIASES = {'C':'Character','V':'Inventory','W':'WorldObject','O':'Organization','A':'Activity'}

def surface(root):
    result = {}
    for path in (Path(root) / 'garrysmod/gamemodes/gincy/gamemode/core').glob('*.lua'):
        for name, arguments in re.findall(r'function\s+((?:Gincy\.[\w.]+|[CVWOA]:\w+))\(([^)]*)\)', path.read_text()):
            for short, full in ALIASES.items():
                if name.startswith(short + ':'):
                    name = full + name[1:]
            result.setdefault(name, {'arguments': arguments, 'source': path.name, 'realm': 'Server' if path.name.startswith('sv_') else 'Shared'})
    for registry in REGISTRIES:
        for method, args in {'Register':'id, definition','Unregister':'id','Get':'id','Exists':'id','GetAll':''}.items():
            result.setdefault('Gincy.' + registry + '.' + method, {'arguments':args,'source':'sh_api.lua','realm':'Shared'})
    for registry, method, args in [('Movement','RegisterModifier','id, callback'),('Animation','RegisterState','id, definition'),('Director','RegisterRule','id, definition'),('WorldObjects','RegisterType','id, definition'),('Organizations','RegisterType','id, definition'),('Activities','RegisterType','id, definition'),('WorldObjects','GetType','id'),('Organizations','GetType','id'),('Activities','GetType','id')]:
        result.setdefault('Gincy.'+registry+'.'+method, {'arguments':args,'source':'sh_api.lua','realm':'Shared'})
    for name,args in {'GetLogger':'','Hook':'event, id, callback','Timer':'id, delay, repetitions, callback','Require':'capability','Shutdown':''}.items():
        result['Context:'+name]={'arguments':args,'source':'sh_api.lua','realm':'Shared'}
    for name,args in {'Register':'kind, id, definition','Get':'kind, id','Exists':'kind, id','GetAll':'kind','Find':'query'}.items():
        result['Content:'+name]={'arguments':args,'source':'sh_api.lua','realm':'Shared'}
    for level in ['Info','Warn','Error','Debug']:
        result['Logger:'+level]={'arguments':'message','source':'sh_api.lua','realm':'Shared'}
    for name,args in {'Get':'key, callback','Set':'key, value, callback'}.items():
        result['StorageHandle:'+name]={'arguments':args,'source':'sv_database.lua','realm':'Server'}
    return result
