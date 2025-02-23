package edu.epam.fop.io;

import java.io.*;
import java.util.ArrayList;
import java.util.List;

public class LicenseReader {

  private String processFile(File file) throws IOException {
    boolean headerEnded = false;
    String licenseName = null, issuer = null, issuedOn = null, expiresOn = null;

    try (BufferedReader bufferedReader = new BufferedReader(new FileReader(file))) {
      String line;
      if (!bufferedReader.readLine().trim().equals("---")) return "";

      while ((line = bufferedReader.readLine()) != null) {
        if (line.trim().equals("---")) {
          headerEnded = true;
          break;
        }
        if (line.startsWith("License:")) {
          licenseName = line.substring("License:".length()).trim();
        } else if (line.startsWith("Issued by:")) {
          issuer = line.substring("Issued by:".length()).trim();
        } else if (line.startsWith("Issued on:")) {
          issuedOn = line.substring("Issued on:".length()).trim();
        } else if (line.startsWith("Expires on:")) {
          expiresOn = line.substring("Expires on:".length()).trim();
        }
      }

      if (!headerEnded || licenseName == null || issuer == null || issuedOn == null) {
        throw new IllegalArgumentException();
      }

      if (expiresOn == null) {
        expiresOn = "unlimited";
      }

      return "License for " + file.getName() + " is " + licenseName + " issued by " + issuer + " [" + issuedOn + " - " + expiresOn + "]";
    }
  }

  private List<File> processDir(File directory) {
    List<File> files = new ArrayList<>();
    File[] f = directory.listFiles();
    if (f == null) return files;

    for (File file : f) {
      if (file.isDirectory()) files.addAll(processDir(file));
      else files.add(file);
    }
    return files;
  }

  public void collectLicenses(File root, File outputFile) {
    if (outputFile == null || root == null || !root.exists() || !root.canRead() || !root.canExecute()) {
      throw new IllegalArgumentException();
    }

    try (BufferedWriter bufferedWriter = new BufferedWriter(new FileWriter(outputFile, false))) {
      if (root.isDirectory()) {
        List<File> files = processDir(root);
        if (files.isEmpty()) throw new IOException();

        for (File file : files) {
          String line = processFile(file);
          if (!line.isEmpty()) {
            bufferedWriter.write(line);
            bufferedWriter.newLine();
          }
        }
      } else {
        String line = processFile(root);
        if (!line.isEmpty()) {
          bufferedWriter.write(line);
        }
      }
    } catch (IOException e) {
      throw new IllegalArgumentException();
    }
  }
}
