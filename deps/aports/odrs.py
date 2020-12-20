import datetime
import pathlib
import plistlib
import os
import shutil
import subprocess
import hashlib
import ctypes

target_build_dir = pathlib.Path(os.environ['TARGET_BUILD_DIR'])
resources_dir = target_build_dir/os.environ['UNLOCALIZED_RESOURCES_FOLDER_PATH']

if os.environ['ENABLE_ON_DEMAND_RESOURCES'] == 'YES':
    raise Exception('This script collides with Xcode\'s ODR processing')

def main():
    if os.environ['ENABLE_APK_ODRS'] != 'YES':
        (resources_dir/'OnDemandResources.plist').unlink(missing_ok=True)
        return

    root = pathlib.Path(os.environ['SRCROOT'])/'deps'/'aports'
    repo_paths = [root/repo/'x86' for repo in ['main', 'community']]

    # get all the files in the repo
    repo_files = []
    for repo_path in repo_paths:
        for file in repo_path.iterdir():
            if file.name == 'APKINDEX.tar.gz':
                continue
            repo_files.append((file, file.stat().st_size))
    repo_files.sort(key=lambda f: f[1])

    # batch them into asset packs
    
    MIN_PACK_SIZE = 5200000
    def is_pack_full(pack_size, tags, files):
        return pack_size > MIN_PACK_SIZE

    def file_pack(file):
        tag = ':'.join(file.relative_to(root).parts)
        files = {tag: str(file)}
        return [tag], files
        
    packs = []
    tags, files = [], {}
    pack_size = 0
    for file, size in repo_files:
        # App Store Connect will reject an app that contains asset packs larger than 512MB. Just give up on such files for now. At the time of writing, this excludes texmf-dist-fontsextra and supertuxkart-data.
        if size > 500000000:
            continue
        new_tags, new_files = file_pack(file)
        tags.extend(new_tags)
        files.update(new_files)
        pack_size += size
        if is_pack_full(pack_size, tags, files):
            packs.append((tags, files))
            tags, files = [], {}
            pack_size = 0
    if tags or files:
        packs.append((tags, files))

    # APKINDEX gets its own pack
    for repo_path in repo_paths:
        packs.append(file_pack(repo_path/'APKINDEX.tar.gz'))

    print('collected', len(packs), 'packs containing', sum(len(files) for tags, files in packs), 'files')
    if len(packs) > 1000:
        raise Exception(f'too many packs! {len(packs)}, app store limit is 1000')
    process_odrs(packs)

# packs: [([tag], {resource_name: file_name}])]
# OnDemandResources.plist: {
#   NSBundleResourceRequestTags: {tag: [asset_pack]}
#   NSBundleResourceRequestAssetPacks: {asset_pack: [filename]}
# }
# AssetPackManifestTemplate.plist: [{'bundleKey': asset_pack, 'URL': url}]
def process_odrs(packs):
    odrs_dir = pathlib.Path(os.environ.get('ASSET_PACK_FOLDER_PATH', target_build_dir/'OnDemandResources'))
    odrs_dir.mkdir(exist_ok=True)

    old_pack_manifests = {}
    manifest = resources_dir/'AssetPackManifestTemplate.plist'
    if manifest.exists():
        with manifest.open('rb') as f:
            manifest = plistlib.load(f)
        for pack in manifest['resources']:
            old_pack_manifests[pack['bundleKey']] = pack

    packs = [(gen_pack_id(tags), tags, files) for (tags, files) in packs]
    odr_tags_plist = {}
    odr_packs_plist = {}
    odrs_plist = {
        'NSBundleResourceRequestTags': odr_tags_plist,
        'NSBundleResourceRequestAssetPacks': odr_packs_plist,
    }
    packs_manifest_plist = {'resources': []}
    for pack_id, tags, files in packs:
        # collect stats
        newest_mtime = 0
        total_size = 0
        for file, file_src in files.items():
            file_src = files[file] = os.path.join(os.environ['SRCROOT'], file_src)
            file_stat = os.stat(file_src)
            total_size += file_stat.st_size
            if newest_mtime < file_stat.st_mtime:
                newest_mtime = file_stat.st_mtime

        # add pack to OnDemandResources.plist
        for tag in tags:
            if tag not in odr_tags_plist:
                odr_tags_plist[tag] = {'NSAssetPacks': []}
            odr_tags_plist[tag]['NSAssetPacks'].append(pack_id)
        odr_packs_plist[pack_id] = list(files.keys())

        # check if this pack can be skipped
        if pack_id in old_pack_manifests:
            old_mtime = old_pack_manifests[pack_id]['primaryContentHash']['hash']
            old_mtime = datetime.datetime.fromisoformat(old_mtime).timestamp()
            # use an epsilon here because the timestamp->isoformat->timestamp roundtrip is lossy
            if old_mtime + 0.000001 >= newest_mtime:
                packs_manifest_plist['resources'].append(old_pack_manifests[pack_id])
                continue

        # materialize pack
        pack_path = odrs_dir/(pack_id+'.assetpack')
        if pack_path.exists():
            shutil.rmtree(pack_path)
        pack_path.mkdir()
        with (pack_path/'Info.plist').open('wb') as f:
            plistlib.dump({
                'CFBundleIdentifier': pack_id,
                'Tags': tags,
            }, f, fmt=plistlib.FMT_BINARY)
            total_size += f.tell()
        for file, file_src in files.items():
            copy_file(pathlib.Path(file_src), pack_path/file)
        packs_manifest_plist['resources'].append({
            'URL': f'http://127.0.0.1{pack_path.resolve()}',
            'bundleKey': pack_id,
            'isStreamable': True,
            'primaryContentHash': {
                'hash': datetime.datetime.fromtimestamp(newest_mtime).isoformat(),
                'strategy': 'modtime',
            },
            'uncompressedSize': total_size,
        })

    with (resources_dir/'OnDemandResources.plist').open('wb') as f:
        plistlib.dump(odrs_plist, f, fmt=plistlib.FMT_BINARY)
    with (resources_dir/'AssetPackManifestTemplate.plist').open('wb') as f:
        plistlib.dump(packs_manifest_plist, f, fmt=plistlib.FMT_BINARY)

def gen_pack_id(tags):
    bundle_id = os.environ['PRODUCT_BUNDLE_IDENTIFIER']
    return bundle_id + '.asset.' + hashlib.md5('+'.join(tags).encode()).hexdigest()

def copy_file(src, dst):
    # apfs clone?
    try:
        clonefile = ctypes.CDLL(None, use_errno=True).clonefile
        clonefile.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int]
    except Exception:
        raise
        shutil.copyfile(src, dst)
        return
    res = clonefile(bytes(src), bytes(dst), 0)
    if res == -1 and ctypes.get_errno() != 0:
        raise os.OSError(ctypes.get_errno())

if __name__ == '__main__':
    main()
