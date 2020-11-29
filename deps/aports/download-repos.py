#!/usr/bin/env python3
import urllib.request
import urllib.parse
import tarfile
import pathlib
import concurrent.futures

IX_NAME = 'P'
IX_VERSION = 'V'

def read_index(index):
    index = index.read()
    packages = []
    for block in index.split(b'\n\n'):
        if block == b'': continue
        package = {}
        for line in block.split(b'\n'):
            key, _, value = line.partition(b':')
            package[key.decode()] = value.decode()
        packages.append(package)
    return packages

def download_repo(root_url, repo_name, index_name):
    repo = pathlib.Path(repo_name)
    repo.mkdir(parents=True, exist_ok=True)
    index_path = repo/'APKINDEX.tar.gz'
    index_url = f'{root_url}/{repo_name}/{index_name}'
    urllib.request.urlretrieve(index_url, index_path)
    with tarfile.open(index_path) as tar:
        pkgs = read_index(tar.extractfile('APKINDEX'))

    downloads = []
    for pkg in pkgs:
        pkg_file = f'{pkg[IX_NAME]}-{pkg[IX_VERSION]}.apk'
        url = f'{root_url}/{repo_name}/{urllib.parse.quote(pkg_file)}'
        path = repo/pkg_file
        if path.exists(): continue
        downloads.append((url, path))
    download_many(downloads)

def download_one(url, path):
    n = 0
    while True:
        try:
            return urllib.request.urlretrieve(url, path)
        except Exception as e:
            print('retrying', url, e)
            n += 1
            if n >= 3: raise

def download_many(downloads):
    done = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=30) as pool:
        futures = [pool.submit(download_one, url, path) for url, path in downloads]
        for f in concurrent.futures.as_completed(futures):
            path, headers = f.result()
            done += 1
            print(f'{done}/{len(futures)}', str(path))

if __name__ == '__main__':
    download_repo('https://f001.backblazeb2.com/file/alpine-archive', 'main/x86', 'APKINDEX-v3.12-2020-11-15.tar.gz')
    download_repo('https://f001.backblazeb2.com/file/alpine-archive', 'community/x86', 'APKINDEX-v3.12-2020-11-15.tar.gz')
