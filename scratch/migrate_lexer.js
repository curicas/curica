import fs from 'fs';
import path from 'path';

const SRC_DIR = path.resolve('references/development/features/approved/Parser_and_Lexer');
const DEST_DIR = path.resolve('references/development/features/completed/Parser_and_Lexer');

if (!fs.existsSync(DEST_DIR)) {
    fs.mkdirSync(DEST_DIR, { recursive: true });
}

if (fs.existsSync(SRC_DIR)) {
    const files = fs.readdirSync(SRC_DIR, { withFileTypes: true });

    files.forEach(file => {
        if (file.isFile() && file.name.endsWith('.md')) {
            const srcPath = path.join(SRC_DIR, file.name);
            const destPath = path.join(DEST_DIR, file.name);
            
            let content = fs.readFileSync(srcPath, 'utf-8');
            content = content.replace('**Status**: Approved', '**Status**: Completed');
            
            fs.writeFileSync(destPath, content);
            fs.unlinkSync(srcPath);
            console.log(`Moved and updated ${file.name} to completed/Parser_and_Lexer/`);
        }
    });
    
    // Clean up empty dir
    if (fs.readdirSync(SRC_DIR).length === 0) {
        fs.rmdirSync(SRC_DIR);
    }
}
