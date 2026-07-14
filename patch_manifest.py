import xml.etree.ElementTree as ET
import yaml, re, os

NS = 'http://schemas.android.com/apk/res/android'
ET.register_namespace('android', NS)

src = 'work/picovr_smali/AndroidManifest.xml'
tree = ET.parse(src)
root = tree.getroot()

# Remove sharedUserId (namespaced key)
ns_key = '{%s}sharedUserId' % NS
if ns_key in root.attrib:
    del root.attrib[ns_key]

# Update platform build attrs
for key in list(root.attrib.keys()):
    if key.endswith('platformBuildVersionCode'):
        root.attrib[key] = '24'
    if key.endswith('platformBuildVersionName'):
        root.attrib[key] = '7.0'

# Add exported to components with intent-filter but no exported
for tag in ['activity','receiver','service','provider']:
    for el in root.iter(tag):
        has_filter = any(True for _ in el.iter('intent-filter'))
        exported = None
        for k in el.attrib:
            if k.endswith('}exported'):
                exported = el.attrib[k]
        if has_filter and exported is None:
            el.set('{%s}exported' % NS, 'true')

# Ensure application attributes exist for legacy storage and cleartext
app = next(root.iter('application'))
app.set('{%s}requestLegacyExternalStorage' % NS, 'true')
app.set('{%s}usesCleartextTraffic' % NS, 'true')
app.set('{%s}extractNativeLibs' % NS, 'true')

# Remove duplicate permission entries
seen = set()
to_remove = []
for perm in root.iter('uses-permission'):
    name = None
    for k in perm.attrib:
        if k.endswith('}name'):
            name = perm.attrib[k]
    if name in seen:
        to_remove.append(perm)
    else:
        seen.add(name)
for perm in to_remove:
    root.remove(perm)

tree.write(src, xml_declaration=True, encoding='utf-8')

# Update apktool.yml sdkInfo
yml_path = 'work/picovr_smali/apktool.yml'
with open(yml_path) as f:
    data = yaml.safe_load(f)
data.setdefault('sdkInfo', {})['minSdkVersion'] = 16
data['sdkInfo']['targetSdkVersion'] = 24
with open(yml_path, 'w') as f:
    yaml.dump(data, f, default_flow_style=False, sort_keys=False)

print('Patched manifest and apktool.yml written.')
