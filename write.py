import os

def save_code_to_file(target_directory, output_filename):
    if not os.path.exists(target_directory):
        print(f"Error: The directory '{target_directory}' does not exist.")
        return

    EXCLUDE_MAP = {
        ".": ["build", "tools"]
    }

    try:
        with open(output_filename, 'w', encoding='utf-8') as out_f:
            for root, dirs, files in os.walk(target_directory):
                rel_root = os.path.relpath(root, target_directory).replace(os.sep, '/')
                
                if rel_root in EXCLUDE_MAP:
                    for to_remove in EXCLUDE_MAP[rel_root]:
                        if to_remove in dirs:
                            dirs.remove(to_remove)
                            print(f"Skipping directory: {rel_root}/{to_remove}")

                for file in files:
                    if file.endswith(('.S', '.c', '.h', '.lds', 'Makefile', 'Kconfig')) and not file.endswith(('all_code.txt')):
                        file_path = os.path.join(root, file)
                        display_path = os.path.relpath(file_path, target_directory).replace(os.sep, '/')
                        
                        try:
                            with open(file_path, 'r', encoding='utf-8') as in_f:
                                content = in_f.read()
                            
                            out_f.write(f"{display_path}:\n```\n{content}")
                            if not content.endswith('\n'):
                                out_f.write('\n') 
                            out_f.write("```\n\n")
                            print(f"Processed: {display_path}")
                            
                        except Exception as e:
                            print(f"Error reading {display_path}: {e}")
                            
        print(f"\nSuccess! Saved to '{output_filename}'.")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    save_code_to_file(".", "all_code.txt")
