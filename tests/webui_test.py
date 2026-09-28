import json
import tempfile
import threading
from pathlib import Path
from playwright.sync_api import sync_playwright
from gincy_sdk.panel import make_server

with tempfile.TemporaryDirectory() as directory:
    server = make_server(directory, port=0, token='browser-test-token-' * 3)
    serving = threading.Thread(target=server.serve_forever, daemon=True)
    serving.start()
    stopping = threading.Event()
    root = Path(directory) / 'garrysmod/data/gincy/management'
    operations = []
    modules = {'modern_hud': {'manifest': {'version': '1.0.0', 'author': 'Gincy', 'dependencies': {}}, 'state': {'state': 'loaded'}, 'dependents': []}}
    content = {'types': {'fish_species': {'owner': 'fishing', 'schema': {'type': 'object', 'fields': {'name': {'type': 'string'}, 'weight': {'type': 'number', 'min': 0}}}}}, 'definitions': {'fish_species': {'carp': {'name': 'Carp', 'weight': 3}}}, 'versions': {}}
    def runtime():
        while not stopping.is_set():
            for path in (root / 'requests').glob('*.json'):
                request = json.loads(path.read_text())
                path.unlink()
                operation, args = request['operation'], request['arguments']
                operations.append(operation)
                if operation == 'status':
                    value = {'version': 'UI TEST FIXTURE', 'os': 'linux', 'architecture': 'x64', 'native': False, 'storage': 'offline', 'modules': modules, 'network': {'rejected': 0}, 'watcher': False, 'map': 'fixture'}
                elif operation in ('modules', 'graph'):
                    value = modules
                elif operation == 'module.disable':
                    modules[args['id']]['state']['state'] = 'disabled'
                    value = True
                elif operation == 'content':
                    value = content
                elif operation == 'content.edit':
                    content['definitions'][args['kind']][args['id']] = args['definition']
                    value = True
                else:
                    value = [] if operation == 'history' else {}
                temporary = root / 'responses' / (path.stem + '.tmp')
                temporary.write_text(json.dumps({'ok': True, 'value': value}))
                temporary.replace(root / 'responses' / path.name)
            stopping.wait(0.01)
    worker = threading.Thread(target=runtime, daemon=True)
    worker.start()
    try:
        with sync_playwright() as api:
            browser = api.chromium.launch(headless=True)
            page = browser.new_page(viewport={'width': 1440, 'height': 960})
            errors = []
            page.on('pageerror', lambda error: errors.append(str(error)))
            page.goto(f'http://127.0.0.1:{server.server_port}')
            page.locator('#token').fill('browser-test-token-' * 3)
            page.locator('#login button').click()
            page.get_by_text('● Connected', exact=True).wait_for()
            page.get_by_role('button', name='Modules', exact=True).click()
            page.get_by_role('button', name='disable', exact=True).click()
            page.get_by_role('cell', name='disabled', exact=True).wait_for()
            page.get_by_role('button', name='Content', exact=True).click()
            page.get_by_role('button', name='Edit', exact=True).click()
            page.get_by_label('weight', exact=True).fill('7')
            page.get_by_role('button', name='Validate & save').click()
            page.wait_for_function("document.querySelector('#view').textContent.includes('7')")
            assert content['definitions']['fish_species']['carp']['weight'] == 7
            for name in ('Network', 'Profiler', 'Errors', 'Dependencies', 'Packages', 'Deployment'):
                page.get_by_role('button', name=name, exact=True).click()
                page.wait_for_timeout(150)
            page.set_viewport_size({'width': 390, 'height': 844})
            assert not errors, errors
            assert 'module.disable' in operations and 'content.edit' in operations
            browser.close()
        print('PASS browser UI: auth, modules, schema form, navigation, responsive layout')
    finally:
        stopping.set()
        worker.join(2)
        server.shutdown()
        server.server_close()
        serving.join(2)
