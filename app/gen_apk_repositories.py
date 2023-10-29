import os

def trim(x, start, end):
    assert x.startswith(start)
    assert x.endswith(end)
    return x[len(start):-len(end)]

APK_REPOSITORIES = [
    'v3.14/main/x86',
    'v3.14/community/x86',
]

repos_file = []
for repo in APK_REPOSITORIES:
    with open(f'{os.environ["SRCROOT"]}/deps/aports/{repo}/index.txt') as f:
        index_name = f.read()
    index_name = trim(index_name, 'APKINDEX-', '.tar.gz\n')
    repos_file.append(f'http://apk.ish.app/{index_name}/{repo}')

with open(os.path.join(os.environ['BUILT_PRODUCTS_DIR'], os.environ['CONTENTS_FOLDER_PATH'], 'repositories.txt'), 'w') as f:
    for line in repos_file:
        print(line, file=f)
