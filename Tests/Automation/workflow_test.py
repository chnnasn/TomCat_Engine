"""Run only through Run-AutomationRegression.ps1 against its disposable workspace."""
import json
import os
from pathlib import Path
import time
import subprocess
import urllib.request
import urllib.error
import uuid


class Client:
    def __init__(self):
        self.http = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        self.url = 'http://127.0.0.1:' + os.environ['TOMCAT_AUTOMATION_PORT']
        self.project = os.environ.get('TOMCAT_PROJECT', '')
        self.session = None

    def send(self, path, payload=None):
        request = urllib.request.Request(self.url + path,
            data=None if payload is None else json.dumps(payload, ensure_ascii=False, allow_nan=False).encode(),
            headers={'Authorization': 'Bearer ' + os.environ['TOMCAT_AUTOMATION_TOKEN'], 'Content-Type': 'application/json'})
        with self.http.open(request, timeout=180) as response:
            return json.load(response)

    def raw(self, tool, **args):
        status = self.send('/health')['data']
        assert self.session is None or status['session_id'] == self.session
        self.session = status['session_id']
        assert Path(status['project']).resolve() == Path(self.project).resolve(), status
        payload = dict(tool=tool, arguments=args, project=self.project, session_id=self.session,
                       scene_version=status['scene_version'], request_id=uuid.uuid4().hex)
        result = self.send('/call', payload)
        if result['ok'] and tool in {'project_create', 'project_open'}:
            self.project = result['data']['project']
        return result

    def call(self, tool, **args):
        result = self.raw(tool, **args)
        assert result['ok'], (tool, result, self.raw('console_get_entries'))
        return result['data']

    def fails(self, tool, code, **args):
        result = self.raw(tool, **args)
        assert not result['ok'] and result['error']['code'] == code, (tool, result)


def main():
    assert 'TomCat-Automation-' in os.environ['TOMCAT_AUTOMATION_WORKSPACE']
    c = Client()
    deadline = time.monotonic() + 60
    while True:
        try:
            c.send('/health')
            break
        except OSError:
            assert time.monotonic() < deadline
            time.sleep(.25)

    def compile_current():
        c.call('script_compile')
        deadline = time.monotonic() + 120
        while not c.call('script_get_status')['current']:
            assert time.monotonic() < deadline, c.call('console_get_entries')
            time.sleep(.2)

    try:
        c.http.open(c.url + '/health')
        raise AssertionError('Unauthenticated health request was accepted')
    except urllib.error.HTTPError as error:
        assert error.code == 403

    if c.project:
        compile_current()
        c.call('scene_save')
    c.fails('project_create', 'INVALID_PATH', path='../escape/Project.tcproj', name='Escape')
    c.call('project_create', path='Created/Project.tcproj', name='Automation Workflow', template='2D')
    assert Path(c.project).is_file()
    catalog_path = Path(__file__).resolve().parents[2]/'docs'/'AutomationTools.json'
    if catalog_path.is_file():
        catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
        assert set(c.call('automation_get_capabilities')['tools']) == {tool['name'] for tool in catalog}
    schema = {v['name']: v for v in c.call('component_get_schema')['components']}
    Path(os.environ['TOMCAT_AUTOMATION_WORKSPACE'], 'schema.json').write_text(json.dumps(schema, indent=2), encoding='utf-8')

    def create(name, components=()):
        eid = c.call('entity_create', name=name)['id']
        for comp in components:
            c.call('component_add', entity_id=eid, component_id=schema[comp]['id'])
        return eid

    def prop(component, name):
        return next(p['id'] for p in schema[component]['properties'] if p['name'] == name)

    def set_value(eid, component, name, value):
        c.call('component_set', entity_id=eid, component_id=schema[component]['id'], property_id=prop(component,name), value=value)

    camera = create('Camera', ['TomCat.Camera'])
    actor = create('Automated Actor', ['TomCat.SpriteRenderer'])
    canvas = create('Canvas', ['TomCat.Canvas', 'TomCat.UIEventSystem'])
    set_value(canvas, 'TomCat.Canvas', 'ScaleMode', 0)
    field = create('Input', ['TomCat.RectTransform', 'TomCat.UIInputField'])
    c.call('entity_reparent', entity_id=field, parent_id=canvas)
    set_value(field, 'TomCat.RectTransform', 'AnchorMin', [0,1])
    set_value(field, 'TomCat.RectTransform', 'AnchorMax', [0,1])
    set_value(field, 'TomCat.RectTransform', 'Pivot', [0,1])
    set_value(field, 'TomCat.RectTransform', 'AnchoredPosition', [10,-60])
    set_value(field, 'TomCat.RectTransform', 'SizeDelta', [200,30])
    scroll = create('Scroll', ['TomCat.RectTransform', 'TomCat.UIScrollView'])
    c.call('entity_reparent', entity_id=scroll, parent_id=canvas)
    for name in ('AnchorMin','AnchorMax','Pivot'):
        set_value(scroll, 'TomCat.RectTransform', name, [0,1])
    set_value(scroll, 'TomCat.RectTransform', 'AnchoredPosition', [10,-110])
    set_value(scroll, 'TomCat.RectTransform', 'SizeDelta', [200,80])
    set_value(scroll, 'TomCat.UIScrollView', 'ContentSize', [200,400])
    c.call('asset_create_directory', path='Art')
    # Binary source assets are staged by the test runner, then imported through
    # the same service an AI uses after generating/downloading project content.
    texture_path = Path(c.project).parent/'Assets'/'Art'/'White.ppm'
    texture_path.write_bytes(b'P6\n2 2\n255\n'+b'\xff'*12)
    texture = c.call('asset_import', path='Art/White.ppm')['handle']
    c.call('asset_validate', handle=texture)
    tilemap = create('Tiles', ['TomCat.Tilemap2D'])
    cells = [{'x':0,'y':0,'sprite':texture}, {'x':1,'y':0,'sprite':texture,'rotation':1}]
    assert c.call('tilemap_set_cells', entity_id=tilemap, cells=cells)['cell_count'] == 2
    assert c.call('tilemap_set_cells', entity_id=tilemap, cells=cells)['cell_count'] == 2
    clip = {'TomCatAnimationClip': {'Version':1,'Name':'Idle','Loop':True,'SampleRate':12,'Frames':[{'SpriteHandle':int(texture),'DurationSeconds':.1}]}}
    clip_handle = c.call('asset_write_text', path='Idle.tcanim', text=json.dumps(clip))['handle']
    c.call('asset_validate', handle=clip_handle)
    controller = {'TomCatAnimatorController': {'Version':1,'InitialState':'Idle','Parameters':[], 'States':[{'Name':'Idle','ClipHandle':int(clip_handle),'Speed':1}], 'Transitions':[]}}
    controller_handle = c.call('asset_write_text', path='Idle.tccontroller', text=json.dumps(controller))['handle']
    c.call('asset_validate', handle=controller_handle)
    animated = create('Animated', ['TomCat.SpriteRenderer','TomCat.SpriteAnimator'])
    set_value(animated, 'TomCat.SpriteAnimator', 'Controller', controller_handle)
    c.call('project_set_settings', tags=['Untagged','TestActor'])
    assert 'TestActor' in c.call('project_get_settings')['tags']
    c.call('player_set_settings', company='TomCatAutomationTests', width=640, height=360)
    assert c.call('player_get_settings')['width'] == 640
    assert 'loaded' in c.call('module_list')
    code = '''using TomCat;
public sealed class AutomationProbe : MonoBehaviour
{
    private void FixedUpdate()
    { var dt = Time.fixedDeltaTime;
        if (Input.IsKeyHeld(KeyCode.D))
        {
            Vector3 p = Transform.Position;
            p.X += 1.0f;
            Transform.Position = p;
        }
    }
}
'''
    script = c.call('asset_write_text', path='AutomationProbe.cs', text=code)['handle']
    c.fails('asset_write_text', 'ASSET_CHANGED', path='AutomationProbe.cs', text='bad')
    c.fails('asset_write_text', 'INVALID_PATH', path='../outside.cs', text='bad')
    compile_current()
    c.call('script_attach', entity_id=actor, handle=script)
    prefab = c.call('prefab_save', entity_id=actor, path='Actor.tcprefab')['handle']
    instance = c.call('prefab_instantiate', handle=prefab)['id']
    prefab_path = Path(c.project).parent/'Assets'/'Actor.tcprefab'
    original_prefab = prefab_path.read_bytes()
    set_value(instance, 'TomCat.SpriteRenderer', 'Color', [.2,.3,.4,1])
    assert c.call('prefab_get_overrides', entity_id=instance)['paths']
    c.call('prefab_action', entity_id=instance, action='revert_property', target_id=instance,
           component_id=schema['TomCat.SpriteRenderer']['id'], property_id=prop('TomCat.SpriteRenderer','Color'))
    set_value(instance, 'TomCat.SpriteRenderer', 'Color', [.2,.3,.4,1])
    c.call('prefab_action', entity_id=instance, action='apply')
    assert prefab_path.read_bytes() != original_prefab
    c.call('history_undo')
    assert prefab_path.read_bytes() == original_prefab
    c.call('history_redo')
    assert prefab_path.read_bytes() != original_prefab
    c.call('prefab_update', entity_id=instance, revert=True)
    c.call('entity_delete', entity_id=instance)
    c.call('history_undo')
    c.call('entity_get', entity_id=instance)
    c.call('history_redo')
    c.call('scene_save_as', path='Main.tomcat')
    scene = c.call('asset_import', path='Main.tomcat')['handle']
    c.fails('asset_delete', 'DELETE_FAILED', handle=scene)
    c.call('asset_move', handle=scene, path='Renamed.tomcat')
    c.call('scene_save')
    assert not (Path(c.project).parent/'Assets'/'Main.tomcat').exists()
    c.call('asset_move', handle=scene, path='Main.tomcat')
    c.call('scene_new')
    create('Additive Scene Marker')
    c.call('scene_save_as', path='Additive.tomcat')
    additive = c.call('asset_import', path='Additive.tomcat')['handle']
    c.call('scene_open', path='Main.tomcat')
    c.call('build_set_scenes', entry_scene=scene, scenes=[{'handle':scene,'enabled':True},{'handle':additive,'enabled':True}])
    before = c.call('scene_get_archive')['document']
    c.fails('scene_set_archive', 'EDIT_REJECTED', document='invalid: true')
    assert c.call('scene_get_archive')['document'] == before
    c.call('runtime_start', width=640, height=360)
    c.call('runtime_step', events=[{'type':'key','code':68,'action':'press'}], frames=3)
    c.call('runtime_assert', entity_id=actor, component_id=schema['TomCat.Transform']['id'],
           property_id=prop('TomCat.Transform','Translation'), value=[3,0,0], tolerance=.0001)
    held = c.call('runtime_get_input')
    assert held['keys_held'] == [68] and held['keys_pressed'] == [] and held['frame'] == 3, held
    c.fails('runtime_step', 'INVALID_ARGUMENT', events=[{'type':'key','code':68,'action':'release'}, {'type':'invalid'}])
    assert c.call('runtime_get_input') == held
    c.call('runtime_step', events=[{'type':'key','code':68,'action':'release'},{'type':'text','text':'自动化🙂'}])
    assert c.call('runtime_get_input')['text'] == '自动化🙂'
    c.call('runtime_step', frames=2)
    c.call('runtime_assert', entity_id=actor, component_id=schema['TomCat.Transform']['id'],
           property_id=prop('TomCat.Transform','Translation'), value=[3,0,0], tolerance=.0001)
    ui = {v['entity_id']:v for v in c.call('runtime_get_ui')['elements']}
    x,y = ui[field]['center']
    c.call('runtime_step', events=[{'type':'pointer','x':x,'y':y},{'type':'button','code':0,'action':'press'}])
    c.call('runtime_step', events=[{'type':'button','code':0,'action':'release'},{'type':'text','text':'中文🙂'}])
    c.call('runtime_assert', entity_id=field, component_id=schema['TomCat.UIInputField']['id'],property_id=prop('TomCat.UIInputField','Text'),value='中文🙂')
    c.call('runtime_step', events=[{'type':'key','code':259,'action':'press'}])
    c.call('runtime_step', events=[{'type':'key','code':259,'action':'release'}])
    c.call('runtime_assert', entity_id=field, component_id=schema['TomCat.UIInputField']['id'],property_id=prop('TomCat.UIInputField','Text'),value='中文')
    x,y = ui[scroll]['center']
    c.call('runtime_step', events=[{'type':'pointer','x':x,'y':y},{'type':'scroll','x':0,'y':-2}])
    scroll_properties = c.call('entity_get', entity_id=scroll)['components']
    scroll_component = next(v for v in scroll_properties if v['name']=='TomCat.UIScrollView')
    assert next(p['value'] for p in scroll_component['properties'] if p['name']=='Offset')[1] > 0
    assert c.call('runtime_animator', entity_id=animated, action='status')['state']=='Idle'
    capture = Path(c.call('runtime_capture')['path'])
    assert capture.read_bytes().startswith(b'P6\n640 360\n255\n')
    assert len(set(capture.read_bytes()[capture.read_bytes().index(b'255\n')+4:])) > 1
    c.call('save_write', slot='test', data_version=1, text='isolated 中文')
    assert bytes.fromhex(c.call('save_read',slot='test')['payload_hex']).decode() == 'isolated 中文'
    assert c.call('save_list')['slots'][0]['slot']=='test'
    c.call('save_delete',slot='test')
    assert c.call('save_list')['slots']==[]
    c.call('profiler_set_recording', enabled=True)
    c.call('runtime_scene_load', handle=additive, additive=True)
    deadline=time.monotonic()+20
    while additive not in c.call('runtime_scene_status')['loaded']:
        assert time.monotonic()<deadline, c.call('runtime_scene_status')
        c.call('runtime_step')
        time.sleep(.01)
    c.call('runtime_scene_unload', handle=additive)
    c.call('runtime_step')
    assert additive not in c.call('runtime_scene_status')['loaded']
    c.call('profiler_set_recording', enabled=False)
    assert c.call('profiler_get_frames')['frames']
    assert json.loads(Path(c.call('profiler_export')['path']).read_text())['traceEvents']
    c.call('editor_stop')
    after = c.call('scene_get_archive')['document']
    assert after == before, 'Runtime changes leaked into the authoring archive'
    c.call('runtime_assert', entity_id=actor, component_id=schema['TomCat.Transform']['id'],
           property_id=prop('TomCat.Transform','Translation'), value=[0,0,0], tolerance=.0001)
    c.call('scene_save')
    result = c.call('build_player', development=False)
    output = Path(result['output_directory'])
    exe = next(output.glob('*.exe'))
    package = next(output.glob('*.tcpak'))
    subprocess.run([str(exe), '--validate-package', str(package)], cwd=output, timeout=120, check=True)
    print('PASS create -> author -> compile -> attach -> prefab -> save -> input/steps/assert -> stop -> build', result, flush=True)


if __name__ == '__main__':
    main()
