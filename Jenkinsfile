stage('Build'){
    packpack = new org.tarantool.packpack()

    matrix = packpack.filterMatrix(
        packpack.default_matrix,
        {!(it['OS'] == 'ubuntu' && it['DIST'] == 'xenial') &&
         !(it['OS'] == 'ubuntu' && it['DIST'] == 'yakkety') &&
         !(it['OS'] == 'fedora' && it['DIST'] == 'rawhide') &&
         !(it['OS'] == 'fedora' && it['DIST'] == '25') &&
         !(it['OS'] == 'debian' && it['DIST'] == 'stretch')})

    node {
        checkout scm
        packpack.prepareSources()
    }
    packpack.packpackBuildMatrix('result', matrix)
}
