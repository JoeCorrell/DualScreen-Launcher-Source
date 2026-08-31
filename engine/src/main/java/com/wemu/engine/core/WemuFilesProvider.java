package com.wemu.engine.core;

import android.content.Context;
import android.content.pm.ProviderInfo;
import android.content.res.AssetFileDescriptor;
import android.database.Cursor;
import android.database.MatrixCursor;
import android.graphics.Point;
import android.os.CancellationSignal;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract.Document;
import android.provider.DocumentsContract.Root;
import android.provider.DocumentsProvider;
import android.webkit.MimeTypeMap;

import androidx.preference.PreferenceManager;

import com.wemu.engine.R;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedList;
import java.util.Locale;
import java.util.Objects;
import java.util.Set;

public class WemuFilesProvider extends DocumentsProvider {
    private static final String ALL_MIME_TYPES = "*/*";
    private boolean enabled;
    private File BASE_DIR;

    @Override
    public void attachInfo(Context context, ProviderInfo info) {
        super.attachInfo(context, info);
        try {
            BASE_DIR = new File(context.getFilesDir(), "imagefs").getCanonicalFile();
            if (!BASE_DIR.isDirectory() && !BASE_DIR.mkdirs()) {
                throw new IOException("Could not create the container-files root");
            }
        } catch (IOException e) {
            throw new IllegalStateException("Could not resolve the document-provider root", e);
        }
        enabled = PreferenceManager.getDefaultSharedPreferences(context).getBoolean("enable_file_provider", true);
    }

    @Override
    public String moveDocument(String sourceDocumentId, String sourceParentDocumentId, String targetParentDocumentId) throws FileNotFoundException {
        File source = getFileForDocId(sourceDocumentId);
        File sourceParent = getDirectoryForDocId(sourceParentDocumentId);
        File targetParent = getDirectoryForDocId(targetParentDocumentId);

        if (!Objects.equals(source.getParentFile(), sourceParent))
            throw new FileNotFoundException("Source has wrong parent: " + sourceDocumentId + " " + sourceParentDocumentId);
        if (source.equals(BASE_DIR))
            throw new FileNotFoundException("The provider root cannot be moved");

        File target = childForName(targetParent, source.getName());
        if (target.exists())
            throw new FileNotFoundException("Target already exist");

        boolean ret = source.renameTo(target);
        if (!ret)
            throw new FileNotFoundException("Failed to move: " + sourceDocumentId);

        return target.getAbsolutePath();
    }

    @Override
    public void removeDocument(String documentId, String parentDocumentId) throws FileNotFoundException {
        File parent = getDirectoryForDocId(parentDocumentId);
        File target = getFileForDocId(documentId);
        if (!Objects.equals(target.getParentFile(), parent) || target.equals(BASE_DIR))
            throw new FileNotFoundException("Document is not a removable child of its parent");

        boolean ret = target.delete();
        if (!ret)
            throw new FileNotFoundException("Failed to delete file: " + documentId);
    }

    private static final String[] DEFAULT_ROOT_PROJECTION = new String[]{
            Root.COLUMN_ROOT_ID,
            Root.COLUMN_MIME_TYPES,
            Root.COLUMN_FLAGS,
            Root.COLUMN_ICON,
            Root.COLUMN_TITLE,
            Root.COLUMN_SUMMARY,
            Root.COLUMN_DOCUMENT_ID,
            Root.COLUMN_AVAILABLE_BYTES
    };

    private static final String[] DEFAULT_DOCUMENT_PROJECTION = new String[]{
            Document.COLUMN_DOCUMENT_ID,
            Document.COLUMN_MIME_TYPE,
            Document.COLUMN_DISPLAY_NAME,
            Document.COLUMN_LAST_MODIFIED,
            Document.COLUMN_FLAGS,
            Document.COLUMN_SIZE
    };

    @Override
    public Cursor queryRoots(String[] projection) {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_ROOT_PROJECTION);
        if (!enabled) return result;
        final String applicationName = getContext().getString(R.string.app_name);

        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(Root.COLUMN_ROOT_ID, getDocIdForFile(BASE_DIR));
        row.add(Root.COLUMN_DOCUMENT_ID, getDocIdForFile(BASE_DIR));
        row.add(Root.COLUMN_SUMMARY, null);
        row.add(Root.COLUMN_FLAGS, Root.FLAG_SUPPORTS_CREATE | Root.FLAG_SUPPORTS_SEARCH | Root.FLAG_SUPPORTS_IS_CHILD);
        row.add(Root.COLUMN_TITLE, applicationName);
        row.add(Root.COLUMN_MIME_TYPES, ALL_MIME_TYPES);
        row.add(Root.COLUMN_AVAILABLE_BYTES, BASE_DIR.getFreeSpace());
        row.add(Root.COLUMN_ICON, appIconResId());
        return result;
    }

    /**
     * The application's own launcher icon.
     *
     * Resolved from the package rather than named as `R.mipmap.ic_launcher`.
     * The icon belongs to `:app`, and a library that hard-codes the resource
     * name of something it does not ship only compiles by accident — it breaks
     * the moment the application renames or replaces it.
     */
    private int appIconResId() {
        final Context context = getContext();
        if (context == null) return 0;
        return context.getApplicationInfo().icon;
    }

    @Override
    public Cursor queryDocument(String documentId, String[] projection) throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION);
        includeFile(result, documentId, null);
        return result;
    }

    @Override
    public Cursor queryChildDocuments(String parentDocumentId, String[] projection, String sortOrder) throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION);
        final File parent = getDirectoryForDocId(parentDocumentId);
        File[] children = parent.listFiles();
        if (children == null) return result;
        for (File file : children) {
            try {
                includeFile(result, null, file);
            } catch (FileNotFoundException ignored) {
                // Hide symlinks whose canonical target leaves the app root.
            }
        }
        return result;
    }

    @Override
    public ParcelFileDescriptor openDocument(final String documentId, String mode, CancellationSignal signal) throws FileNotFoundException {
        final File file = getFileForDocId(documentId);
        final int accessMode = ParcelFileDescriptor.parseMode(mode);
        return ParcelFileDescriptor.open(file, accessMode);
    }

    @Override
    public AssetFileDescriptor openDocumentThumbnail(String documentId, Point sizeHint, CancellationSignal signal) throws FileNotFoundException {
        final File file = getFileForDocId(documentId);
        final ParcelFileDescriptor pfd = ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
        return new AssetFileDescriptor(pfd, 0, file.length());
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public String createDocument(String parentDocumentId, String mimeType, String displayName) throws FileNotFoundException {
        File parent = getDirectoryForDocId(parentDocumentId);
        validateDisplayName(displayName);
        File newFile = childForName(parent, displayName);
        int noConflictId = 2;
        while (newFile.exists()) {
            newFile = childForName(parent, displayName + " (" + noConflictId++ + ")");
        }
        try {
            boolean succeeded;
            if (Document.MIME_TYPE_DIR.equals(mimeType)) {
                succeeded = newFile.mkdir();
            } else {
                succeeded = newFile.createNewFile();
            }
            if (!succeeded) {
                throw new FileNotFoundException("Failed to create document with id " + newFile.getPath());
            }
        } catch (IOException e) {
            throw new FileNotFoundException("Failed to create document with id " + newFile.getPath());
        }
        return newFile.getPath();
    }

    @Override
    public void deleteDocument(String documentId) throws FileNotFoundException {
        File file = getFileForDocId(documentId);
        if (file.equals(BASE_DIR)) throw new FileNotFoundException("The provider root cannot be deleted");
        if (!file.delete()) {
            throw new FileNotFoundException("Failed to delete document with id " + documentId);
        }
    }

    @Override
    public String getDocumentType(String documentId) throws FileNotFoundException {
        File file = getFileForDocId(documentId);
        return getMimeType(file);
    }

    @Override
    public Cursor querySearchDocuments(String rootId, String query, String[] projection) throws FileNotFoundException {
        final MatrixCursor result = new MatrixCursor(projection != null ? projection : DEFAULT_DOCUMENT_PROJECTION);
        final File parent = getFileForDocId(rootId);

        final LinkedList<File> pending = new LinkedList<>();
        final Set<String> visitedDirectories = new HashSet<>();
        pending.add(parent);
        final String needle = query == null ? "" : query.toLowerCase(Locale.ROOT);

        final int MAX_SEARCH_RESULTS = 50;
        while (!pending.isEmpty() && result.getCount() < MAX_SEARCH_RESULTS) {
            final File file = pending.removeFirst();
            try {
                File safe = requireInsideBase(file, true);
                if (safe.isDirectory()) {
                    if (!visitedDirectories.add(safe.getAbsolutePath())) continue;
                    File[] children = safe.listFiles();
                    if (children != null) Collections.addAll(pending, children);
                } else if (safe.getName().toLowerCase(Locale.ROOT)
                        .contains(needle)) {
                    includeFile(result, null, safe);
                }
            } catch (IOException e) {
                // A broken or escaping link is not part of this provider.
            }
        }

        return result;
    }

    @Override
    public boolean isChildDocument(String parentDocumentId, String documentId) {
        try {
            File parent = requireInsideBase(new File(parentDocumentId), true);
            File document = requireInsideBase(new File(documentId), true);
            return !parent.equals(document) && document.toPath().startsWith(parent.toPath());
        } catch (IOException e) {
            return false;
        }
    }

    private static String getDocIdForFile(File file) {
        return file.getAbsolutePath();
    }

    private File getFileForDocId(String docId) throws FileNotFoundException {
        return requireInsideBase(new File(docId), true);
    }

    private File getDirectoryForDocId(String docId) throws FileNotFoundException {
        File directory = getFileForDocId(docId);
        if (!directory.isDirectory()) throw new FileNotFoundException(directory + " is not a directory");
        return directory;
    }

    private File childForName(File parent, String displayName) throws FileNotFoundException {
        validateDisplayName(displayName);
        return requireInsideBase(new File(parent, displayName), false);
    }

    private static void validateDisplayName(String displayName) throws FileNotFoundException {
        if (displayName == null || displayName.trim().isEmpty() || ".".equals(displayName)
                || "..".equals(displayName) || displayName.indexOf('/') >= 0
                || displayName.indexOf('\\') >= 0 || displayName.chars().anyMatch(ch -> ch < 0x20)
                || !new File(displayName).getName().equals(displayName)) {
            throw new FileNotFoundException("Invalid document name");
        }
    }

    private File requireInsideBase(File candidate, boolean mustExist) throws FileNotFoundException {
        try {
            File canonical = candidate.getCanonicalFile();
            if (!canonical.toPath().startsWith(BASE_DIR.toPath()))
                throw new FileNotFoundException("Document leaves the provider root");
            if (mustExist && !canonical.exists())
                throw new FileNotFoundException(canonical + " not found");
            return canonical;
        } catch (IOException e) {
            FileNotFoundException failure = new FileNotFoundException("Could not resolve document path");
            failure.initCause(e);
            throw failure;
        }
    }

    private static String getMimeType(File file) {
        if (file.isDirectory()) {
            return Document.MIME_TYPE_DIR;
        } else {
            final String name = file.getName();
            final int lastDot = name.lastIndexOf('.');
            if (lastDot >= 0) {
                final String extension = name.substring(lastDot + 1).toLowerCase(Locale.ROOT);
                final String mime = MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
                if (mime != null) return mime;
            }
            return "application/octet-stream";
        }
    }

    @Override
    public String renameDocument(String documentId, String displayName) throws FileNotFoundException {
        File oldFile = getFileForDocId(documentId);
        if (oldFile.equals(BASE_DIR)) throw new FileNotFoundException("The provider root cannot be renamed");
        validateDisplayName(displayName);

        File parentDir = oldFile.getParentFile();
        File newFile = childForName(parentDir, displayName);
        if (newFile.exists()) throw new FileNotFoundException("A document with that name already exists");

        if (oldFile.renameTo(newFile)) {
            return newFile.getAbsolutePath();
        } else {
            throw new FileNotFoundException("Failed to rename document with id " + documentId);
        }
    }

    private void includeFile(MatrixCursor result, String docId, File file) throws FileNotFoundException {
        if (!enabled)
            throw new FileNotFoundException();

        if (docId == null) {
            file = requireInsideBase(file, true);
            docId = getDocIdForFile(file);
        } else {
            file = getFileForDocId(docId);
        }

        int flags = 0;
        if (file.isDirectory()) {
            if (file.canWrite()) flags |= Document.FLAG_DIR_SUPPORTS_CREATE;
        } else if (file.canWrite()) {
            flags |= Document.FLAG_SUPPORTS_WRITE;
        }
        if (!file.equals(BASE_DIR) && file.getParentFile() != null && file.getParentFile().canWrite())
            flags |= Document.FLAG_SUPPORTS_DELETE;

        // Add support for renaming files and directories
        if (!file.equals(BASE_DIR) && file.canWrite()) {
            flags |= Document.FLAG_SUPPORTS_RENAME;
        }

        final String displayName = file.getName();
        final String mimeType = getMimeType(file);
        if (mimeType.startsWith("image/")) flags |= Document.FLAG_SUPPORTS_THUMBNAIL;

        final MatrixCursor.RowBuilder row = result.newRow();
        row.add(Document.COLUMN_DOCUMENT_ID, docId);
        row.add(Document.COLUMN_DISPLAY_NAME, displayName);
        row.add(Document.COLUMN_SIZE, file.length());
        row.add(Document.COLUMN_MIME_TYPE, mimeType);
        row.add(Document.COLUMN_LAST_MODIFIED, file.lastModified());
        row.add(Document.COLUMN_FLAGS, flags);
        row.add(Document.COLUMN_ICON, appIconResId());
    }

}
