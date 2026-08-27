package com.fuckingbms.cast

import android.content.ContentProvider
import android.content.ContentValues
import android.content.Context
import android.database.Cursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import java.io.File
import java.io.FileNotFoundException

/**
 * 最小 FileProvider 实现（项目零第三方依赖，不引入 androidx.core）：
 * 只暴露 cacheDir/update/ 下的文件，供系统安装器读取下载的 APK。
 */
class UpdateFileProvider : ContentProvider() {

    override fun onCreate(): Boolean = true

    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        val file = fileFor(uri) ?: throw FileNotFoundException("unknown uri: $uri")
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY)
    }

    override fun getType(uri: Uri): String? = "application/vnd.android.package-archive"

    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?, sortOrder: String?): Cursor? = null

    override fun insert(uri: Uri, values: ContentValues?): Uri? = null

    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0

    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?): Int = 0

    private fun fileFor(uri: Uri): File? {
        val segments = uri.pathSegments ?: return null
        if (segments.isEmpty()) return null
        val root = File(context!!.cacheDir, "update").canonicalFile
        return File(root, segments.joinToString(File.separator)).takeIf { candidate ->
            candidate.canonicalFile.toPath().startsWith(root.toPath())
        }
    }

    companion object {
        fun getUriForFile(context: Context, authority: String, file: File): Uri {
            val root = File(context.cacheDir, "update").canonicalFile
            val target = file.canonicalFile
            if (!target.toPath().startsWith(root.toPath())) {
                throw IllegalArgumentException("file outside cache/update: $file")
            }
            val relative = root.toPath().relativize(target.toPath())
            val builder = Uri.Builder().scheme("content").authority(authority)
            relative.forEach { builder.appendPath(it.toString()) }
            return builder.build()
        }
    }
}
