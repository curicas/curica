import os

base_dir = '/run/media/user/development/JS/Curica-Runtime-Dev/references/skills'

skills = {
    'c-development-guidelines': {
        'file': 'C_Development_Guidelines.md',
        'name': 'C Development Guidelines',
        'description': 'Guidelines and constraints for writing C code in the Curica Runtime, including Cosmopolitan Libc rules and C99 standards.'
    },
    'memory-management-rules': {
        'file': 'Memory_Management_Rules.md',
        'name': 'Memory Management Rules',
        'description': 'Rules for handling memory in Curica, including NaN-boxing, arena allocation, and garbage collection tracing.'
    },
    'event-loop-and-async-io': {
        'file': 'Event_Loop_and_Async_IO.md',
        'name': 'Event Loop and Async I/O',
        'description': 'How to interface native modules with the custom event loop, non-blocking I/O polling, and POSIX thread pools.'
    },
    'module-creation-workflow': {
        'file': 'Module_Creation_Workflow.md',
        'name': 'Module Creation Workflow',
        'description': 'Step-by-step workflow for creating native modules, including the C bindings, JS wrappers, and integration into the builtin module loader.'
    },
    'feature-development-guide': {
        'file': 'Feature_Development_Guide.md',
        'name': 'Curica Architecture and Feature Development',
        'description': 'A high-level architectural overview of the Curica Javascript runtime, its capabilities, and standard library overview.'
    }
}

for folder, info in skills.items():
    folder_path = os.path.join(base_dir, folder)
    os.makedirs(folder_path, exist_ok=True)
    
    old_file_path = os.path.join(base_dir, info['file'])
    with open(old_file_path, 'r') as f:
        content = f.read()
    
    frontmatter = f"---\nname: {info['name']}\ndescription: {info['description']}\n---\n\n"
    new_content = frontmatter + content
    
    with open(os.path.join(folder_path, 'SKILL.md'), 'w') as f:
        f.write(new_content)
    
    os.remove(old_file_path)

print("Skills converted successfully.")
