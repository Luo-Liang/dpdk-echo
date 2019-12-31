import zipfile
import argparse
import glob

parser = argparse.ArgumentParser()
parser.add_argument('--folder', type=str, default='build')
parser.add_argument('--file-prefix', type=str, default='mp-*')
parser.add_argument('--output-relative-file', type=str, default='build/current.zip')

args = parser.parse_args()

list_files = glob.glob(args.folder + "/" + args.file_prefix)
print(list_files)
with zipfile.ZipFile(args.output_relative_file, 'w') as zipMe:        
    for file in list_files:
        zipMe.write(file, compress_type=zipfile.ZIP_DEFLATED)
        pass
    pass
